idf_component_register(SRCS "imgui_reload_main.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES app_ui hotreload
                                 esp32_p4_function_ev_board
                                 esp_lcd esp_event nvs_flash esp_netif)
