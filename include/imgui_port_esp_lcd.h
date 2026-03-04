#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for imgui esp_lcd port
 */
typedef struct {
    esp_lcd_panel_handle_t panel_handle; /*!< Initialized LCD panel handle */
    int width;                           /*!< Display width in pixels */
    int height;                          /*!< Display height in pixels */

    /**
     * @brief Optional external render buffer (array of width*height color32_t = RGBA8888).
     *
     * If NULL the port allocates one internally, preferring PSRAM.
     * Supply an external buffer when you want to avoid a heap allocation –
     * for example by using the QEMU panel's dedicated frame buffer returned by
     * esp_lcd_rgb_qemu_get_frame_buffer().
     */
    void *render_buf;

    /**
     * @brief When true, the render buffer is passed to esp_lcd_panel_draw_bitmap
     *        as-is (no RGBA→RGB565 conversion).  Use this when the panel accepts
     *        32-bit pixels (e.g. QEMU RGB panel in BPP_32 mode).
     *        When false (default), an RGB565 conversion is performed first.
     */
    bool direct_output;

    /**
     * @brief When true, swap the Red and Blue channels before output.
     *
     * The software renderer produces RGBA8888 (R at byte-0).  Some display
     * controllers or host systems (e.g. QEMU's SDL window in BPP_32 mode)
     * expect BGRA8888 (B at byte-0).  Set this to true in that case.
     * Applies to both direct_output and RGB565 conversion paths.
     */
    bool swap_rb;
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

#ifdef __cplusplus
}
#endif
