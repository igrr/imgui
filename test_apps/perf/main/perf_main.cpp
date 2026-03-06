/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 *
 * ImGui performance benchmark test app.
 *
 * Renders a representative UI scene and logs the FPS value
 * to the console for automated collection by pytest.
 */

#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "imgui.h"
#include "imgui_port_esp_lcd.h"

static const char *TAG = "imgui_perf";

static void draw_test_scene(void)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Perf Test", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Performance benchmark scene");
    ImGui::Separator();

    static float color[3] = {0.4f, 0.7f, 1.0f};
    ImGui::ColorEdit3("Color", color);

    static float slider_val = 0.5f;
    ImGui::SliderFloat("Slider", &slider_val, 0.0f, 1.0f);

    static int counter = 0;
    if (ImGui::Button("Click me")) {
        counter++;
    }
    ImGui::SameLine();
    ImGui::Text("Count: %d", counter);

    static bool checkbox = true;
    ImGui::Checkbox("Enable feature", &checkbox);

    static int radio = 0;
    ImGui::RadioButton("Option A", &radio, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Option B", &radio, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Option C", &radio, 2);

    static float progress = 0.6f;
    ImGui::ProgressBar(progress, ImVec2(-1, 0), "60%");

    ImGui::Separator();
    static char text_buf[128] = "Edit me!";
    ImGui::InputText("Text input", text_buf, sizeof(text_buf));

    ImGui::End();
}

extern "C" void app_main(void)
{
    bsp_lcd_handles_t lcd_handles;
    ESP_ERROR_CHECK(bsp_display_new(&lcd_handles));

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
    imgui_port_enable_fps_counter_console();

    ESP_LOGI(TAG, "ImGui Perf Test Ready");

    while (true) {
        imgui_port_new_frame();
        draw_test_scene();
        imgui_port_render();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
