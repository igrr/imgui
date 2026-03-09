idf_component_register(
    SRCS
       "imgui_port_esp_lcd.cpp"
       "imgui/imgui.cpp"
       "imgui/imgui_draw.cpp"
       "imgui/imgui_tables.cpp"
       "imgui/imgui_widgets.cpp"
       "imgui/imgui_demo.cpp"
    INCLUDE_DIRS "include" "imgui"
    PRIV_INCLUDE_DIRS "."
    REQUIRES "esp_lcd"
    PRIV_REQUIRES "esp_timer"
)

target_compile_definitions(__idf_imgui PRIVATE
    IMGUI_USER_CONFIG="imgui_esp_config.h"
)

# When profiling is enabled, use linker --wrap to intercept upstream ImGui
# functions (C++ Itanium-ABI mangled names) so we can add timing without
# modifying the imgui submodule.
if(CONFIG_IMGUI_PROFILING)
    target_link_options(__idf_imgui INTERFACE
        -Wl,--wrap=_ZN5ImGui8NewFrameEv    # ImGui::NewFrame()
        -Wl,--wrap=_ZN5ImGui6RenderEv      # ImGui::Render()
    )
endif()
