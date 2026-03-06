#include "imgui.h"
#include "app_ui.h"

void app_ui_draw(void)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Hot Reload Demo", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Hello from app_ui!");
    ImGui::Separator();

    ImGui::Text("Welcome to the Hot Reload Demo!");
    ImGui::Separator();

    ImGui::Text("This code is recompiled and reloaded live!");
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
