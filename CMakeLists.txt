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
