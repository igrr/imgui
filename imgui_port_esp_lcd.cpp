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
#include "softraster/softraster.h"

#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <cstring>
#include <new>

static const char *TAG = "imgui_port";

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
    if (!out) return ESP_ERR_INVALID_ARG;
    auto *r = new (std::nothrow) imgui_port_renderer_t;
    if (!r) return ESP_ERR_NO_MEM;
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
/*  Software renderer                                                          */
/* -------------------------------------------------------------------------- */

/**
 * Rasterize all ImGui draw lists into @p screen using the ImSoft softraster.
 *
 * Modernised version of ImSoft's renderDrawLists() – avoids the deprecated
 * ScaleClipRects() call (scaling is already handled by imgui when
 * DisplayFramebufferScale is (1,1)).
 */
static void sw_render(ImDrawData *draw_data, texture_t<color32_t> &screen)
{
    ImGuiIO &io   = ImGui::GetIO();
    int fb_width  = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0) return;

    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
        const ImDrawList *cmd_list   = draw_data->CmdLists[n];
        const ImDrawVert *vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx  *idx_buffer = cmd_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
            const ImDrawCmd &pcmd = cmd_list->CmdBuffer[cmd_i];

            if (pcmd.UserCallback) {
                pcmd.UserCallback(cmd_list, &pcmd);
            } else {
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
    if (!cfg || !cfg->renderer) return ESP_ERR_INVALID_ARG;

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
            if (!s_render_buf_ext) heap_caps_free(s_render_buf);
            s_render_buf = nullptr;
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG, "Allocated LCD buffer: %zu kB", lcd_bytes / 1024);
    }

    /* ---- ImGui context ---- */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize             = ImVec2((float)s_width, (float)s_height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    /* ---- Font atlas ---- */
    unsigned char *atlas_pixels = nullptr;
    int atlas_w = 0, atlas_h = 0;
    io.Fonts->GetTexDataAsAlpha8(&atlas_pixels, &atlas_w, &atlas_h);

    s_font_tex = new (std::nothrow) texture_alpha8_t();
    if (!s_font_tex) {
        ESP_LOGE(TAG, "Failed to alloc font texture wrapper");
        if (!s_render_buf_ext) heap_caps_free(s_render_buf);
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
    if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 60.0f;
    s_last_us = now_us;

    ImGui::NewFrame();
}

extern "C" void imgui_port_render(void)
{
    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdListsCount == 0) return;

    /* Clear render buffer to opaque black before each frame */
    const int n_pixels = s_width * s_height;
    for (int i = 0; i < n_pixels; ++i) {
        s_render_buf[i] = color32_t(0, 0, 0, 255);
    }

    /* Build a softraster texture_t view over the render buffer */
    texture_t<color32_t> screen;
    screen.type     = texture_type_t::COLOR32;
    screen.pixels   = s_render_buf;
    screen.w        = (size_t)s_width;
    screen.h        = (size_t)s_height;
    screen.size     = sizeof(color32_t);
    screen.needFree = false;

    /* Rasterise all draw lists */
    sw_render(draw_data, screen);

    /* Convert and flush to panel */
    if (s_renderer->convert) {
        if (s_renderer->output_bpp == 32) {
            /* In-place conversion (e.g. R↔B swap for ARGB8888) */
            s_renderer->convert(s_render_buf, s_render_buf, n_pixels);
            esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_render_buf);
        } else {
            /* Convert to separate output buffer (RGB888, RGB565, …) */
            s_renderer->convert(s_render_buf, s_lcd_buf, n_pixels);
            esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_lcd_buf);
        }
    } else {
        /* No conversion – render buffer is already in the target format */
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, s_width, s_height, s_render_buf);
    }
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
