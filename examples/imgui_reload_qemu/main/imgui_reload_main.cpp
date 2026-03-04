/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 *
 * ImGui Hot Reload example.
 *
 * Combines QEMU LCD + ImGui rendering with the hotreload component
 * to demonstrate live-updating UI code.
 */

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_qemu_rgb.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "hotreload.h"

#include "imgui.h"
#include "imgui_port_esp_lcd.h"
#include "app_ui.h"

static const char *TAG = "imgui_reload";

#define LCD_W  800
#define LCD_H  480

extern "C" void app_main(void)
{
    /* ---- Networking (required for hotreload HTTP server) ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    /* ---- QEMU virtual RGB panel (32-bit) ---- */
    esp_lcd_panel_handle_t panel = nullptr;
    const esp_lcd_rgb_qemu_config_t panel_cfg = {
        .width  = LCD_W,
        .height = LCD_H,
        .bpp    = RGB_QEMU_BPP_32,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_qemu(&panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    void *qemu_fb = nullptr;
    ESP_ERROR_CHECK(esp_lcd_rgb_qemu_get_frame_buffer(panel, &qemu_fb));

    /* ---- ImGui port ---- */
    const imgui_port_cfg_t port_cfg = {
        .panel_handle  = panel,
        .width         = LCD_W,
        .height        = LCD_H,
        .render_buf    = qemu_fb,
        .direct_output = true,
        .swap_rb       = true,
    };
    ESP_ERROR_CHECK(imgui_port_init(&port_cfg));

    /* ---- Hot reload ---- */
    hotreload_config_t hr_config = HOTRELOAD_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(hotreload_load(&hr_config));

    hotreload_server_config_t server_config = HOTRELOAD_SERVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(hotreload_server_start(&server_config));

    ESP_LOGI(TAG, "ImGui Hot Reload Ready");

    /* ---- Render loop ---- */
    while (true) {
        imgui_port_new_frame();
        app_ui_draw();
        imgui_port_render();

        if (hotreload_update_available()) {
            ESP_LOGI(TAG, "Update available, reloading...");
            ESP_ERROR_CHECK(hotreload_reload(&hr_config));
        }

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
