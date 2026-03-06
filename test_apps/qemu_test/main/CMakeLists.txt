idf_component_register(SRCS "imgui_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "esp_lcd" "espressif__imgui")

imgui_font_add(${IMGUI_FONTS_DIR}/Karla-Regular.ttf)
