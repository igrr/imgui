/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 *
 * ImGui port for ESP-IDF using esp_lcd and a software rasterizer.
 *
 * Rendering pipeline:
 *   ImGui draw lists
 *       → software rasterizer (softraster, adapted from LAK132/ImSoft)
 *       → RGBA8888 framebuffer (internal or external)
 *       → [optional] pixel format conversion via renderer
 *       → esp_lcd_panel_draw_bitmap()
 */

#include "imgui_port_esp_lcd.h"

#include "imgui.h"

#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <cinttypes>
#include <cstring>
#include <new>
#include <set>

#include "sdkconfig.h"

static const char *TAG = "imgui_port";

/* -------------------------------------------------------------------------- */
/*  Profiling infrastructure                                                   */
/* -------------------------------------------------------------------------- */

#if CONFIG_IMGUI_PROFILING

/**
 * Per-frame timing breakdown (microseconds).
 *
 * Stages measured via port-layer macros:
 *   clear, rasterize, convert, flush
 *
 * Stages measured via linker wrapping of upstream ImGui code:
 *   new_frame  (ImGui::NewFrame)
 *   render     (ImGui::Render)
 *
 * Rasterization sub-stages measured via softraster hooks:
 *   rast_quads, rast_tris  (plus primitive counts)
 */
static struct {
    /* Top-level stages */
    int64_t new_frame;
    int64_t render;
    int64_t clear;
    int64_t rasterize;
    int64_t convert;
    int64_t flush;
    int64_t total;

    /* Rasterization sub-stages */
    int64_t rast_quads;       /* time in quad (rectangle) rendering */
    int64_t rast_tris;        /* time in triangle rendering */
    int     rast_quad_count;  /* number of quads rendered */
    int     rast_tri_count;   /* number of triangles rendered */
    int     rast_cmd_count;   /* number of draw commands */
    int     rast_elem_count;  /* total index elements processed */

    /* Pixel-level counters (for cost-per-pixel analysis) */
    int64_t quad_pixels;      /* total pixels filled by quads */
    int64_t tri_pixels;       /* total pixels filled by triangles (inside triangle) */
    int64_t tri_bbox_pixels;  /* total bounding-box pixels tested for triangles */

    /* Quad sub-category counts */
    int     quad_solid_count;     /* quads with no texture (solid color fill) */
    int     quad_blit_count;      /* quads with 1:1 texture blit */
    int     quad_scaled_count;    /* quads with scaled texture */
    int64_t quad_solid_pixels;
    int64_t quad_blit_pixels;
    int64_t quad_scaled_pixels;
} s_prof_cur, s_prof_acc;

static int s_prof_frames = 0;

/* Scratch variable for timing a section */
static int64_t s_prof_ts;

#define PROF_BEGIN()        do { s_prof_ts = esp_timer_get_time(); } while (0)
#define PROF_END(field)     do { s_prof_cur.field += esp_timer_get_time() - s_prof_ts; } while (0)

/*
 * Define softraster profiling hooks before including softraster.h.
 * These inject timing around renderQuad/renderTri calls inside the
 * renderCommand() template, avoiding the need to duplicate it.
 */
#define SOFTRASTER_BEFORE_QUAD()  do { int64_t _qt0 = esp_timer_get_time();
#define SOFTRASTER_AFTER_QUAD()   s_prof_cur.rast_quads += esp_timer_get_time() - _qt0; \
                                  s_prof_cur.rast_quad_count++; } while (0)
#define SOFTRASTER_BEFORE_TRI()   do { int64_t _tt0 = esp_timer_get_time();
#define SOFTRASTER_AFTER_TRI()    s_prof_cur.rast_tris += esp_timer_get_time() - _tt0; \
                                  s_prof_cur.rast_tri_count++; } while (0)

/* Pixel-level profiling hooks called from inside softraster render loops */
#define SOFTRASTER_QUAD_PIXELS(n)       do { s_prof_cur.quad_pixels += (n); } while (0)
#define SOFTRASTER_TRI_PIXEL_HIT()      do { s_prof_cur.tri_pixels++; } while (0)
#define SOFTRASTER_TRI_BBOX_PIXELS(n)   do { s_prof_cur.tri_bbox_pixels += (n); } while (0)
#define SOFTRASTER_QUAD_CATEGORY(is_solid, is_blit, px) do { \
    if (is_solid) { s_prof_cur.quad_solid_count++; s_prof_cur.quad_solid_pixels += (px); } \
    else if (is_blit) { s_prof_cur.quad_blit_count++; s_prof_cur.quad_blit_pixels += (px); } \
    else { s_prof_cur.quad_scaled_count++; s_prof_cur.quad_scaled_pixels += (px); } \
} while (0)

#else /* !CONFIG_IMGUI_PROFILING */

#define PROF_BEGIN()        (void)0
#define PROF_END(field)     (void)0

#endif /* CONFIG_IMGUI_PROFILING */

#include "softraster/softraster.h"

/* -------------------------------------------------------------------------- */
/*  Linker-based wrapping of upstream ImGui functions                          */
/*                                                                             */
/*  The --wrap linker flag redirects calls to a symbol S so that:              */
/*    __wrap_S   is called instead of S                                        */
/*    __real_S   calls the original S                                          */
/*                                                                             */
/*  We use this for ImGui::NewFrame() and ImGui::Render() so that we can       */
/*  instrument upstream code without modifying the imgui submodule.            */
/*  The mangled names (Itanium C++ ABI / GCC) are:                            */
/*    ImGui::NewFrame  →  _ZN5ImGui8NewFrameEv                                */
/*    ImGui::Render    →  _ZN5ImGui6RenderEv                                  */
/* -------------------------------------------------------------------------- */

#if CONFIG_IMGUI_PROFILING

extern "C" {

    extern void __real__ZN5ImGui8NewFrameEv(void);
    extern void __real__ZN5ImGui6RenderEv(void);

    void __wrap__ZN5ImGui8NewFrameEv(void)
    {
        PROF_BEGIN();
        __real__ZN5ImGui8NewFrameEv();
        PROF_END(new_frame);
    }

    void __wrap__ZN5ImGui6RenderEv(void)
    {
        PROF_BEGIN();
        __real__ZN5ImGui6RenderEv();
        PROF_END(render);
    }

} /* extern "C" */

#endif /* CONFIG_IMGUI_PROFILING */

/* -------------------------------------------------------------------------- */
/*  Renderer (private definition)                                              */
/* -------------------------------------------------------------------------- */

typedef void (*imgui_port_convert_fn_t)(color32_t *src, void *dst, int n_pixels);

struct imgui_port_renderer_t {
    int output_bpp;                    /* bits per pixel of the output format */
    imgui_port_convert_fn_t convert;   /* NULL → use render buffer as-is */
};

/* -------------------------------------------------------------------------- */
/*  State                                                                      */
/* -------------------------------------------------------------------------- */

static esp_lcd_panel_handle_t s_panel    = nullptr;
static int                    s_width    = 0;
static int                    s_height   = 0;
static int64_t                s_last_us  = 0;

/* RGBA8888 render target.  May be externally supplied or internally allocated. */
static color32_t *s_render_buf     = nullptr;
static bool       s_render_buf_ext = false; /* true → we don't own it */

/* Output buffer for formats that differ from the render buffer (RGB888, RGB565).
 * NULL when the renderer works in-place (32 bpp formats). */
static void      *s_lcd_buf    = nullptr;

/* Renderer configuration */
static imgui_port_renderer_handle_t s_renderer = nullptr;

/* Font atlas wrapped as an ImSoft alpha-8 texture */
static texture_alpha8_t *s_font_tex = nullptr;

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                    */
/* -------------------------------------------------------------------------- */

/** Allocate from PSRAM if available, fall back to internal RAM. */
static void *psram_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        p = heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
    }
    return p;
}

/* -------------------------------------------------------------------------- */
/*  Pixel format conversion functions                                          */
/* -------------------------------------------------------------------------- */

/*
 * ARGB8888 (as a 32-bit word on little-endian: 0xAARRGGBB, i.e. B,G,R,A in
 * memory byte order).  The internal render buffer is ABGR8888 (R,G,B,A in
 * memory), so we swap R↔B in-place.
 */
static void convert_argb8888(color32_t *src, void * /*dst*/, int n)
{
    for (int i = 0; i < n; ++i) {
        uint8_t tmp = src[i].r;
        src[i].r    = src[i].b;
        src[i].b    = tmp;
    }
}

/*
 * RGB888 – strip the alpha byte, writing 3 bytes per pixel.
 */
static void convert_rgb888(color32_t *src, void *dst, int n)
{
    uint8_t *out = static_cast<uint8_t *>(dst);
    for (int i = 0; i < n; ++i) {
        out[i * 3 + 0] = src[i].r;
        out[i * 3 + 1] = src[i].g;
        out[i * 3 + 2] = src[i].b;
    }
}

/*
 * RGB565 – pack to 16-bit.
 */
static void convert_rgb565(color32_t *src, void *dst, int n)
{
    uint16_t *out = static_cast<uint16_t *>(dst);
    for (int i = 0; i < n; ++i) {
        out[i] = (static_cast<uint16_t>(src[i].r >> 3) << 11)
                 | (static_cast<uint16_t>(src[i].g >> 2) <<  5)
                 | (static_cast<uint16_t>(src[i].b >> 3));
    }
}

/* -------------------------------------------------------------------------- */
/*  Renderer factory functions                                                 */
/* -------------------------------------------------------------------------- */

static esp_err_t new_renderer(int bpp, imgui_port_convert_fn_t fn,
                              imgui_port_renderer_handle_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    auto *r = new (std::nothrow) imgui_port_renderer_t;
    if (!r) {
        return ESP_ERR_NO_MEM;
    }
    r->output_bpp = bpp;
    r->convert    = fn;
    *out = r;
    return ESP_OK;
}

extern "C" esp_err_t imgui_port_new_renderer_argb8888(imgui_port_renderer_handle_t *out)
{
    return new_renderer(32, convert_argb8888, out);
}

extern "C" esp_err_t imgui_port_new_renderer_abgr8888(imgui_port_renderer_handle_t *out)
{
    return new_renderer(32, nullptr, out);
}

extern "C" esp_err_t imgui_port_new_renderer_rgb888(imgui_port_renderer_handle_t *out)
{
    return new_renderer(24, convert_rgb888, out);
}

extern "C" esp_err_t imgui_port_new_renderer_rgb565(imgui_port_renderer_handle_t *out)
{
    return new_renderer(16, convert_rgb565, out);
}

extern "C" void imgui_port_delete_renderer(imgui_port_renderer_handle_t handle)
{
    delete handle;
}

/* -------------------------------------------------------------------------- */
/*  Pre-render hooks                                                           */
/* -------------------------------------------------------------------------- */

typedef void (*imgui_port_hook_fn_t)(void);

static std::set<imgui_port_hook_fn_t> s_pre_render_hooks;

/* -------------------------------------------------------------------------- */
/*  FPS counter                                                                */
/* -------------------------------------------------------------------------- */

static void fps_draw_ui(void)
{
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - 4, io.DisplaySize.y - 4),
        ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("##FPS", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::Text("%.0f FPS", io.Framerate);
    }
    ImGui::End();
}

static void fps_print_console(void)
{
    static int64_t s_last_print_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_print_us >= 1000000) {
        ImGuiIO &io = ImGui::GetIO();
        ESP_LOGI(TAG, "FPS: %.1f", io.Framerate);
        s_last_print_us = now_us;
    }
}

extern "C" void imgui_port_enable_fps_counter_ui(void)
{
    s_pre_render_hooks.insert(fps_draw_ui);
}

extern "C" void imgui_port_enable_fps_counter_console(void)
{
    s_pre_render_hooks.insert(fps_print_console);
}

/* -------------------------------------------------------------------------- */
/*  Profiling console output                                                   */
/* -------------------------------------------------------------------------- */

#if CONFIG_IMGUI_PROFILING

static void profiling_print_console(void)
{
    static int64_t s_last_print_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (now_us - s_last_print_us < 1000000 || s_prof_frames == 0) {
        return;
    }
    s_last_print_us = now_us;

    /* Compute averages over the accumulated frames */
    int n = s_prof_frames;
    ESP_LOGI(TAG, "IMGUI_PROF: new_frame=%" PRId64 " render=%" PRId64
             " clear=%" PRId64 " rasterize=%" PRId64
             " convert=%" PRId64 " flush=%" PRId64
             " total=%" PRId64 " frames=%d",
             s_prof_acc.new_frame / n, s_prof_acc.render / n,
             s_prof_acc.clear / n, s_prof_acc.rasterize / n,
             s_prof_acc.convert / n, s_prof_acc.flush / n,
             s_prof_acc.total / n, n);

    /* Rasterization sub-breakdown */
    int64_t rast_overhead = s_prof_acc.rasterize / n
                            - s_prof_acc.rast_quads / n
                            - s_prof_acc.rast_tris / n;
    ESP_LOGI(TAG, "IMGUI_RAST: quads=%" PRId64 " tris=%" PRId64
             " overhead=%" PRId64
             " quad_count=%d tri_count=%d cmd_count=%d elem_count=%d",
             s_prof_acc.rast_quads / n, s_prof_acc.rast_tris / n,
             rast_overhead,
             s_prof_acc.rast_quad_count / n, s_prof_acc.rast_tri_count / n,
             s_prof_acc.rast_cmd_count / n, s_prof_acc.rast_elem_count / n);

    /* Pixel-level analysis */
    int64_t quad_px = s_prof_acc.quad_pixels / n;
    int64_t tri_px = s_prof_acc.tri_pixels / n;
    int64_t tri_bbox_px = s_prof_acc.tri_bbox_pixels / n;
    int64_t quad_us = s_prof_acc.rast_quads / n;
    int64_t tri_us = s_prof_acc.rast_tris / n;
    ESP_LOGI(TAG, "IMGUI_PIXELS: quad_px=%" PRId64 " tri_px=%" PRId64
             " tri_bbox_px=%" PRId64 " tri_fill_ratio=%d%%"
             " quad_ns_per_px=%" PRId64 " tri_ns_per_px=%" PRId64,
             quad_px, tri_px, tri_bbox_px,
             tri_bbox_px ? (int)(tri_px * 100 / tri_bbox_px) : 0,
             quad_px ? (quad_us * 1000 / quad_px) : 0,
             tri_px ? (tri_us * 1000 / tri_px) : 0);

    /* Quad sub-category breakdown */
    ESP_LOGI(TAG, "IMGUI_QUAD_CAT: solid=%d/%d(%" PRId64 "px)"
             " blit=%d/%d(%" PRId64 "px)"
             " scaled=%d/%d(%" PRId64 "px)",
             s_prof_acc.quad_solid_count / n,
             s_prof_acc.rast_quad_count / n,
             s_prof_acc.quad_solid_pixels / n,
             s_prof_acc.quad_blit_count / n,
             s_prof_acc.rast_quad_count / n,
             s_prof_acc.quad_blit_pixels / n,
             s_prof_acc.quad_scaled_count / n,
             s_prof_acc.rast_quad_count / n,
             s_prof_acc.quad_scaled_pixels / n);

    /* Reset accumulators */
    memset(&s_prof_acc, 0, sizeof(s_prof_acc));
    s_prof_frames = 0;
}

#endif /* CONFIG_IMGUI_PROFILING */

extern "C" void imgui_port_enable_profiling_console(void)
{
#if CONFIG_IMGUI_PROFILING
    s_pre_render_hooks.insert(profiling_print_console);
#else
    ESP_LOGW(TAG, "Profiling not enabled — set CONFIG_IMGUI_PROFILING=y");
#endif
}

/* -------------------------------------------------------------------------- */
/*  Software renderer                                                          */
/* -------------------------------------------------------------------------- */

/**
 * Rasterize all ImGui draw lists into @p screen using the ImSoft softraster.
 *
 * Modernised version of ImSoft's renderDrawLists() – avoids the deprecated
 * ScaleClipRects() call (scaling is already handled by imgui when
 * DisplayFramebufferScale is (1,1)).
 *
 * When CONFIG_IMGUI_PROFILING is enabled, the SOFTRASTER_BEFORE/AFTER_QUAD/TRI
 * hooks (defined above, before including softraster.h) automatically collect
 * per-primitive timing inside renderCommand().
 */
static void sw_render(ImDrawData *draw_data, texture_t<color32_t> &screen)
{
    ImGuiIO &io   = ImGui::GetIO();
    int fb_width  = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0) {
        return;
    }

    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
        const ImDrawList *cmd_list   = draw_data->CmdLists[n];
        const ImDrawVert *vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx  *idx_buffer = cmd_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
            const ImDrawCmd &pcmd = cmd_list->CmdBuffer[cmd_i];

            if (pcmd.UserCallback) {
                pcmd.UserCallback(cmd_list, &pcmd);
            } else {
#if CONFIG_IMGUI_PROFILING
                s_prof_cur.rast_cmd_count++;
                s_prof_cur.rast_elem_count += pcmd.ElemCount;
#endif
                renderCommand<int>(
                    screen,
                    reinterpret_cast<const texture_base_t *>(pcmd.GetTexID()),
                    vtx_buffer,
                    idx_buffer,
                    pcmd);
            }
            idx_buffer += pcmd.ElemCount;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  Public C API                                                               */
/* -------------------------------------------------------------------------- */

extern "C" esp_err_t imgui_port_init(const imgui_port_cfg_t *cfg)
{
    if (!cfg || !cfg->renderer) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initialising imgui port (%dx%d, %d bpp output)",
             cfg->width, cfg->height, cfg->renderer->output_bpp);

    s_panel    = cfg->panel_handle;
    s_width    = cfg->width;
    s_height   = cfg->height;
    s_renderer = cfg->renderer;

    /* ---- Render buffer (RGBA8888 / color32_t) ---- */
    static_assert(sizeof(color32_t) == 4, "color32_t must be 4 bytes");

    if (cfg->render_buf) {
        /* Caller provided an external buffer – use it directly. */
        s_render_buf     = static_cast<color32_t *>(cfg->render_buf);
        s_render_buf_ext = true;
        ESP_LOGI(TAG, "Using external render buffer at %p", cfg->render_buf);
    } else {
        const size_t render_bytes = (size_t)s_width * s_height * sizeof(color32_t);
        s_render_buf = static_cast<color32_t *>(psram_alloc(render_bytes));
        if (!s_render_buf) {
            ESP_LOGE(TAG, "Failed to alloc render buffer (%zu bytes)", render_bytes);
            return ESP_ERR_NO_MEM;
        }
        s_render_buf_ext = false;
        ESP_LOGI(TAG, "Allocated render buffer: %zu kB", render_bytes / 1024);
    }

    /* ---- Output buffer – only when the format differs from 32 bpp ---- */
    if (s_renderer->output_bpp < 32) {
        const size_t lcd_bytes = (size_t)s_width * s_height * s_renderer->output_bpp / 8;
        s_lcd_buf = psram_alloc(lcd_bytes);
        if (!s_lcd_buf) {
            ESP_LOGE(TAG, "Failed to alloc LCD buffer (%zu bytes)", lcd_bytes);
            if (!s_render_buf_ext) {
                heap_caps_free(s_render_buf);
            }
            s_render_buf = nullptr;
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "Allocated LCD buffer: %zu kB", lcd_bytes / 1024);
    }

    /* ---- ImGui context ---- */
    IMGUI_CHECKVERSION();
    if (!ImGui::GetCurrentContext()) {
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
    }

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize             = ImVec2((float)s_width, (float)s_height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    /* Use Alpha8 atlas format — the software renderer only needs the alpha
     * channel and this saves 3/4 of the atlas memory vs the RGBA32 default. */
    io.Fonts->TexDesiredFormat = ImTextureFormat_Alpha8;

    /* ---- Font atlas ---- */
    unsigned char *atlas_pixels = nullptr;
    int atlas_w = 0, atlas_h = 0;
    io.Fonts->GetTexDataAsAlpha8(&atlas_pixels, &atlas_w, &atlas_h);

    s_font_tex = new (std::nothrow) texture_alpha8_t();
    if (!s_font_tex) {
        ESP_LOGE(TAG, "Failed to alloc font texture wrapper");
        if (!s_render_buf_ext) {
            heap_caps_free(s_render_buf);
        }
        heap_caps_free(s_lcd_buf);
        s_render_buf = nullptr;
        s_lcd_buf    = nullptr;
        return ESP_ERR_NO_MEM;
    }

    /* Point the ImSoft texture wrapper at imgui's atlas buffer (no extra copy).
     * needFree = false because imgui owns the atlas memory. */
    s_font_tex->type     = texture_type_t::ALPHA8;
    s_font_tex->pixels   = atlas_pixels;
    s_font_tex->w        = (size_t)atlas_w;
    s_font_tex->h        = (size_t)atlas_h;
    s_font_tex->size     = sizeof(alpha8_t);
    s_font_tex->needFree = false;

    io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(s_font_tex));

    s_last_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Ready");
    return ESP_OK;
}

extern "C" void imgui_port_new_frame(void)
{
    ImGuiIO &io = ImGui::GetIO();

    int64_t now_us = esp_timer_get_time();
    io.DeltaTime   = (float)(now_us - s_last_us) / 1e6f;
    if (io.DeltaTime <= 0.0f) {
        io.DeltaTime = 1.0f / 60.0f;
    }
    s_last_us = now_us;

    ImGui::NewFrame();
}

extern "C" void imgui_port_render(void)
{
#if CONFIG_IMGUI_PROFILING
    int64_t frame_start = esp_timer_get_time();
#endif

    for (auto hook : s_pre_render_hooks) {
        hook();
    }

    ImGui::Render();

    ImDrawData *draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdListsCount == 0) {
        return;
    }

    /* Clear render buffer to opaque black before each frame */
    const int n_pixels = s_width * s_height;

    PROF_BEGIN();
    for (int i = 0; i < n_pixels; ++i) {
        s_render_buf[i] = color32_t(0, 0, 0, 255);
    }
    PROF_END(clear);

    /* Build a softraster texture_t view over the render buffer */
    texture_t<color32_t> screen;
    screen.type     = texture_type_t::COLOR32;
    screen.pixels   = s_render_buf;
    screen.w        = (size_t)s_width;
    screen.h        = (size_t)s_height;
    screen.size     = sizeof(color32_t);
    screen.needFree = false;

    /* Rasterise all draw lists */
    PROF_BEGIN();
    sw_render(draw_data, screen);
    PROF_END(rasterize);

    /* Convert and flush to panel */
    if (s_renderer->convert) {
        if (s_renderer->output_bpp == 32) {
            /* In-place conversion (e.g. R↔B swap for ARGB8888) */
            PROF_BEGIN();
            s_renderer->convert(s_render_buf, s_render_buf, n_pixels);
            PROF_END(convert);

            PROF_BEGIN();
            esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_render_buf);
            PROF_END(flush);
        } else {
            /* Convert to separate output buffer (RGB888, RGB565, …) */
            PROF_BEGIN();
            s_renderer->convert(s_render_buf, s_lcd_buf, n_pixels);
            PROF_END(convert);

            PROF_BEGIN();
            esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_lcd_buf);
            PROF_END(flush);
        }
    } else {
        /* No conversion – render buffer is already in the target format */
        PROF_BEGIN();
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_render_buf);
        PROF_END(flush);
    }

#if CONFIG_IMGUI_PROFILING
    s_prof_cur.total = esp_timer_get_time() - frame_start;

    /* Accumulate into per-second averages */
    s_prof_acc.new_frame       += s_prof_cur.new_frame;
    s_prof_acc.render          += s_prof_cur.render;
    s_prof_acc.clear           += s_prof_cur.clear;
    s_prof_acc.rasterize       += s_prof_cur.rasterize;
    s_prof_acc.convert         += s_prof_cur.convert;
    s_prof_acc.flush           += s_prof_cur.flush;
    s_prof_acc.total           += s_prof_cur.total;
    s_prof_acc.rast_quads      += s_prof_cur.rast_quads;
    s_prof_acc.rast_tris       += s_prof_cur.rast_tris;
    s_prof_acc.rast_quad_count += s_prof_cur.rast_quad_count;
    s_prof_acc.rast_tri_count  += s_prof_cur.rast_tri_count;
    s_prof_acc.rast_cmd_count  += s_prof_cur.rast_cmd_count;
    s_prof_acc.rast_elem_count += s_prof_cur.rast_elem_count;
    s_prof_acc.quad_pixels       += s_prof_cur.quad_pixels;
    s_prof_acc.tri_pixels        += s_prof_cur.tri_pixels;
    s_prof_acc.tri_bbox_pixels   += s_prof_cur.tri_bbox_pixels;
    s_prof_acc.quad_solid_count  += s_prof_cur.quad_solid_count;
    s_prof_acc.quad_blit_count   += s_prof_cur.quad_blit_count;
    s_prof_acc.quad_scaled_count += s_prof_cur.quad_scaled_count;
    s_prof_acc.quad_solid_pixels += s_prof_cur.quad_solid_pixels;
    s_prof_acc.quad_blit_pixels  += s_prof_cur.quad_blit_pixels;
    s_prof_acc.quad_scaled_pixels += s_prof_cur.quad_scaled_pixels;
    s_prof_frames++;

    /* Clear for next frame (after accumulation, so new_frame timing from
     * the linker wrapper during imgui_port_new_frame() is preserved). */
    memset(&s_prof_cur, 0, sizeof(s_prof_cur));
#endif
}

extern "C" void imgui_port_deinit(void)
{
    if (s_font_tex) {
        delete s_font_tex;
        s_font_tex = nullptr;
    }
    ImGui::DestroyContext();

    if (!s_render_buf_ext) {
        heap_caps_free(s_render_buf);
    }
    heap_caps_free(s_lcd_buf);
    s_render_buf = nullptr;
    s_lcd_buf    = nullptr;
    s_renderer   = nullptr;
}
