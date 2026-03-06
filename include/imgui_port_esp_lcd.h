/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque renderer handle.
 *
 * Created by one of the imgui_port_new_renderer_*() factory functions.
 * Encapsulates the output pixel format and conversion logic.  Only the
 * factory function that is actually called (and its conversion routine)
 * will be linked into the final binary – unused formats are eliminated
 * by the linker's --gc-sections pass.
 */
typedef struct imgui_port_renderer_t *imgui_port_renderer_handle_t;

/**
 * @brief Create a renderer for ARGB8888 output (32 bpp, B at byte-0 on LE).
 *
 * Swaps R and B channels relative to the internal RGBA8888 render buffer.
 * Suitable for displays that expect BGRA byte order (e.g. QEMU SDL in BPP_32
 * mode, many DRM/KMS framebuffers).
 */
esp_err_t imgui_port_new_renderer_argb8888(imgui_port_renderer_handle_t *out_handle);

/**
 * @brief Create a renderer for ABGR8888 output (32 bpp, R at byte-0 on LE).
 *
 * No conversion – the internal render buffer is already in this format.
 */
esp_err_t imgui_port_new_renderer_abgr8888(imgui_port_renderer_handle_t *out_handle);

/**
 * @brief Create a renderer for RGB888 output (24 bpp, R at byte-0).
 *
 * Strips the alpha channel from the internal RGBA8888 buffer.
 */
esp_err_t imgui_port_new_renderer_rgb888(imgui_port_renderer_handle_t *out_handle);

/**
 * @brief Create a renderer for RGB565 output (16 bpp).
 */
esp_err_t imgui_port_new_renderer_rgb565(imgui_port_renderer_handle_t *out_handle);

/**
 * @brief Delete a renderer previously created by a factory function.
 */
void imgui_port_delete_renderer(imgui_port_renderer_handle_t handle);

/**
 * @brief Configuration for imgui esp_lcd port
 */
typedef struct {
    esp_lcd_panel_handle_t panel_handle; /*!< Initialized LCD panel handle */
    int width;                           /*!< Display width in pixels */
    int height;                          /*!< Display height in pixels */

    /**
     * @brief Optional external render buffer (array of width*height RGBA8888 pixels).
     *
     * If NULL the port allocates one internally, preferring PSRAM.
     * Supply an external buffer when you want to avoid a heap allocation –
     * for example by using the QEMU panel's dedicated frame buffer returned by
     * esp_lcd_rgb_qemu_get_frame_buffer().
     */
    void *render_buf;

    /**
     * @brief Renderer handle defining the output pixel format.
     *
     * Must be created by one of the imgui_port_new_renderer_*() functions
     * before calling imgui_port_init().  The port does NOT take ownership –
     * call imgui_port_delete_renderer() after imgui_port_deinit() if desired.
     */
    imgui_port_renderer_handle_t renderer;
} imgui_port_cfg_t;

/**
 * @brief Initialize imgui with an esp_lcd panel backend.
 *
 * Builds the ImGui context and font atlas, and prepares the software renderer.
 *
 * @param cfg  Port configuration
 * @return ESP_OK on success, ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t imgui_port_init(const imgui_port_cfg_t *cfg);

/**
 * @brief Begin a new ImGui frame.
 *
 * Call this before any ImGui:: widget calls. Updates the delta time.
 */
void imgui_port_new_frame(void);

/**
 * @brief Render the current ImGui frame to the LCD panel.
 *
 * Calls ImGui::Render(), rasterizes the draw lists into an RGBA8888 framebuffer
 * using a software renderer, optionally converts to RGB565, and flushes to
 * the panel via esp_lcd_panel_draw_bitmap().
 */
void imgui_port_render(void);

/**
 * @brief Destroy the imgui context and free internally-allocated resources.
 *
 * Does NOT free an externally-supplied render_buf.
 */
void imgui_port_deinit(void);

/**
 * @brief Enable an on-screen FPS counter overlay.
 *
 * Draws a small translucent window in the lower-right corner of the display
 * showing the current frame rate.  Call this once after imgui_port_init().
 */
void imgui_port_enable_fps_counter_ui(void);

/**
 * @brief Enable FPS logging to the console.
 *
 * Prints the current frame rate via ESP_LOGI once per second.
 * Call this once after imgui_port_init().
 */
void imgui_port_enable_fps_counter_console(void);

#ifdef __cplusplus
}
#endif
