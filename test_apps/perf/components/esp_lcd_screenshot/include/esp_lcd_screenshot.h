/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 *
 * Virtual esp_lcd panel driver that captures draw_bitmap calls
 * and outputs the framebuffer content as base64-encoded data.
 */

#pragma once

#include <stdio.h>
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for the screenshot panel driver
 */
typedef struct {
    int width;              /*!< Panel width in pixels */
    int height;             /*!< Panel height in pixels */
    int bits_per_pixel;     /*!< Bits per pixel of the output format (16, 24, or 32) */
} esp_lcd_screenshot_config_t;

/**
 * @brief Create a virtual screenshot panel driver
 *
 * The driver accepts draw_bitmap calls just like a real LCD panel.
 * Use esp_lcd_screenshot_dump() to output the captured framebuffer.
 *
 * @param config    Panel configuration
 * @param ret_panel Returned panel handle
 * @return ESP_OK on success
 */
esp_err_t esp_lcd_new_screenshot_panel(const esp_lcd_screenshot_config_t *config,
                                       esp_lcd_panel_handle_t *ret_panel);

/**
 * @brief Dump the current framebuffer content as base64 to a FILE stream
 *
 * Outputs the raw pixel data between FRAMEBUFFER_BEGIN and FRAMEBUFFER_END
 * markers, encoded as base64. The FRAMEBUFFER_BEGIN line includes width
 * and height: "FRAMEBUFFER_BEGIN <width> <height> <bpp>"
 *
 * @param panel  Panel handle created by esp_lcd_new_screenshot_panel()
 * @param stream Output stream (e.g. stdout). If NULL, uses stdout.
 * @return ESP_OK on success
 */
esp_err_t esp_lcd_screenshot_dump(esp_lcd_panel_handle_t panel, FILE *stream);

#ifdef __cplusplus
}
#endif
