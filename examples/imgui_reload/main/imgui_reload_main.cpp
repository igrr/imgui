/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 *
 * ImGui Hot Reload example (BSP).
 *
 * Uses BSP display API + ImGui rendering with the hotreload component
 * to demonstrate live-updating UI code on real hardware.
 */

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "hotreload.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "imgui.h"
#include "imgui_port_esp_lcd.h"
#include "app_ui.h"

static const char *TAG = "imgui_reload";

static IRAM_ATTR bool on_color_trans_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *ctx)
{
    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &need_yield);
    return need_yield == pdTRUE;
}

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

    /* ---- BSP display ---- */
    bsp_lcd_handles_t lcd_handles;
    ESP_ERROR_CHECK(bsp_display_new(&lcd_handles));

    /* Register DPI refresh-done callback */
    SemaphoreHandle_t refresh_finish = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_color_trans_done = on_color_trans_done;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(lcd_handles.panel, &cbs, refresh_finish));

    /* ---- ImGui port ---- */
    imgui_port_renderer_handle_t renderer = nullptr;
    ESP_ERROR_CHECK(imgui_port_new_renderer_rgb888(&renderer));

    const imgui_port_cfg_t port_cfg = {
        .panel_handle  = lcd_handles.panel,
        .width         = BSP_LCD_H_RES,
        .height        = BSP_LCD_V_RES,
        .render_buf    = nullptr,
        .renderer      = renderer,
    };
    ESP_ERROR_CHECK(imgui_port_init(&port_cfg));
    imgui_port_enable_fps_counter_ui();
    imgui_port_enable_fps_counter_console();

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
