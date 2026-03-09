/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include "esp_lcd_screenshot.h"
#include "esp_lcd_panel_interface.h"
#include "esp_log.h"
#include "esp_check.h"
#include "mbedtls/base64.h"

static const char *TAG = "lcd_screenshot";

typedef struct {
    esp_lcd_panel_t base;
    int width;
    int height;
    int bytes_per_pixel;
    uint8_t *framebuffer;
} screenshot_panel_t;

static esp_err_t screenshot_draw_bitmap(esp_lcd_panel_t *panel,
                                        int x_start, int y_start,
                                        int x_end, int y_end,
                                        const void *color_data)
{
    screenshot_panel_t *drv = (screenshot_panel_t *)panel;
    int w = x_end - x_start;
    int bpp = drv->bytes_per_pixel;

    const uint8_t *src = (const uint8_t *)color_data;
    for (int y = y_start; y < y_end; y++) {
        uint8_t *dst = drv->framebuffer + (y * drv->width + x_start) * bpp;
        memcpy(dst, src, w * bpp);
        src += w * bpp;
    }
    return ESP_OK;
}

static esp_err_t screenshot_del(esp_lcd_panel_t *panel)
{
    screenshot_panel_t *drv = (screenshot_panel_t *)panel;
    free(drv->framebuffer);
    free(drv);
    return ESP_OK;
}

static esp_err_t screenshot_noop(esp_lcd_panel_t *panel)
{
    return ESP_OK;
}

esp_err_t esp_lcd_new_screenshot_panel(const esp_lcd_screenshot_config_t *config,
                                       esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(config->bits_per_pixel == 16 || config->bits_per_pixel == 24 ||
                        config->bits_per_pixel == 32,
                        ESP_ERR_INVALID_ARG, TAG, "bpp must be 16, 24, or 32");

    screenshot_panel_t *drv = calloc(1, sizeof(screenshot_panel_t));
    ESP_RETURN_ON_FALSE(drv, ESP_ERR_NO_MEM, TAG, "no memory for panel");

    drv->width = config->width;
    drv->height = config->height;
    drv->bytes_per_pixel = config->bits_per_pixel / 8;

    size_t fb_size = (size_t)drv->width * drv->height * drv->bytes_per_pixel;
    drv->framebuffer = calloc(1, fb_size);
    if (!drv->framebuffer) {
        free(drv);
        ESP_LOGE(TAG, "no memory for framebuffer (%zu bytes)", fb_size);
        return ESP_ERR_NO_MEM;
    }

    drv->base.del = screenshot_del;
    drv->base.reset = screenshot_noop;
    drv->base.init = screenshot_noop;
    drv->base.draw_bitmap = screenshot_draw_bitmap;

    *ret_panel = &drv->base;
    ESP_LOGI(TAG, "Screenshot panel created (%dx%d, %d bpp)", config->width, config->height, config->bits_per_pixel);
    return ESP_OK;
}

esp_err_t esp_lcd_screenshot_dump(esp_lcd_panel_handle_t panel, FILE *stream)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG, "invalid panel handle");
    screenshot_panel_t *drv = (screenshot_panel_t *)panel;

    if (!stream) {
        stream = stdout;
    }

    size_t fb_size = (size_t)drv->width * drv->height * drv->bytes_per_pixel;

    /* Lock the stream to prevent interleaving with log output from other tasks */
    flockfile(stream);

    fprintf(stream, "FRAMEBUFFER_BEGIN %d %d %d\n", drv->width, drv->height, drv->bytes_per_pixel * 8);

    /* Encode in chunks: 3072 input bytes → 4096 base64 chars per line */
    const size_t chunk_in = 3072;
    char b64_line[4100];

    for (size_t offset = 0; offset < fb_size; offset += chunk_in) {
        size_t remaining = fb_size - offset;
        size_t this_chunk = remaining < chunk_in ? remaining : chunk_in;
        size_t b64_len = 0;

        mbedtls_base64_encode((unsigned char *)b64_line, sizeof(b64_line),
                              &b64_len, drv->framebuffer + offset, this_chunk);
        b64_line[b64_len] = '\0';
        fputs(b64_line, stream);
        fputc('\n', stream);
    }

    fprintf(stream, "FRAMEBUFFER_END\n");
    fflush(stream);
    funlockfile(stream);
    return ESP_OK;
}
