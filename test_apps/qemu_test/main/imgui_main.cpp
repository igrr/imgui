/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: MIT
 *
 * ImGui QEMU test application.
 *
 * Sets up an 800×480 virtual display via the QEMU RGB panel driver, then runs
 * the ImGui demo window in a loop using the esp_lcd software-renderer port.
 */

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"
#include "esp_err.h"
#include "esp_log.h"

/* ImGui C++ API */
#include "imgui.h"
/* esp_lcd software-renderer port (C API) */
#include "imgui_port_esp_lcd.h"

static const char *TAG = "imgui_test";

#define LCD_W  800
#define LCD_H  480

extern "C" void app_main(void)
{
    /* ------------------------------------------------------------------ */
    /* 1. Create the QEMU virtual RGB panel (32-bit pixel format)          */
    /* ------------------------------------------------------------------ */
    esp_lcd_panel_handle_t panel = nullptr;
    const esp_lcd_rgb_qemu_config_t panel_cfg = {
        .width  = LCD_W,
        .height = LCD_H,
        .bpp    = RGB_QEMU_BPP_32,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_qemu(&panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    /* ------------------------------------------------------------------ */
    /* 2. Get the QEMU dedicated frame buffer.                             */
    /*                                                                     */
    /* This memory lives at 0x20000000 inside QEMU – it is not taken from  */
    /* the ESP32's internal SRAM heap.  We use it as our RGBA8888 render   */
    /* target so that no extra allocation is needed.  direct_output=true   */
    /* tells the port to skip the RGB565 conversion and send the buffer    */
    /* straight to draw_bitmap().                                          */
    /* ------------------------------------------------------------------ */
    void *qemu_fb = nullptr;
    ESP_ERROR_CHECK(esp_lcd_rgb_qemu_get_frame_buffer(panel, &qemu_fb));

    /* ------------------------------------------------------------------ */
    /* 3. Initialise the imgui port                                        */
    /* ------------------------------------------------------------------ */
    const imgui_port_cfg_t port_cfg = {
        .panel_handle  = panel,
        .width         = LCD_W,
        .height        = LCD_H,
        .render_buf    = qemu_fb,
        .direct_output = true,
        /* QEMU's SDL display uses BGRA8888; the renderer produces RGBA8888. */
        .swap_rb       = true,
    };
    ESP_ERROR_CHECK(imgui_port_init(&port_cfg));

    ESP_LOGI(TAG, "Display ImGui Demo");

    /* ------------------------------------------------------------------ */
    /* 4. Render loop                                                      */
    /* ------------------------------------------------------------------ */
    bool show_demo = true;

    while (true) {
        imgui_port_new_frame();

        ImGui::ShowDemoWindow(&show_demo);

        imgui_port_render();

        vTaskDelay(pdMS_TO_TICKS(16)); /* ~60 fps */
    }
}
