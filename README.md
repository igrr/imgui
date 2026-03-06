# ImGui ESP-IDF Component

[Dear ImGui](https://github.com/ocornut/imgui) packaged as an ESP-IDF component, with a software-renderer backend for `esp_lcd` panels.

- Bundles the unmodified upstream Dear ImGui library
- Uses a software rasterizer written by [LAK132](https://github.com/LAK132)
- Works with any LCD supported by `esp_lcd` component
- Can be tested on the host with QEMU

## Usage

Add the component to your project's `idf_component.yml`:

```yaml
dependencies:
  igrr/imgui:
    version: "^1.92.6"
```

Initialize the port in your application:

<!-- code_snippet_start:examples/imgui_reload/main/imgui_reload_main.cpp:r/^#include "imgui\.h"/:r/^#include "app_ui\.h"/ -->

```cpp
#include "imgui.h"
#include "imgui_port_esp_lcd.h"
```

<!-- code_snippet_end -->

<!-- code_snippet_start:examples/imgui_reload/main/imgui_reload_main.cpp:r/ImGui port/:r/imgui_port_init/+ -->

```cpp
    /* ---- ImGui port ---- */
    imgui_port_renderer_handle_t renderer = nullptr;
    ESP_ERROR_CHECK(imgui_port_new_renderer_rgb888(&renderer));

    const imgui_port_cfg_t port_cfg = {
        .panel_handle  = panel,
        .width         = BSP_LCD_H_RES,
        .height        = BSP_LCD_V_RES,
        .render_buf    = nullptr,
        .renderer      = renderer,
    };
    ESP_ERROR_CHECK(imgui_port_init(&port_cfg));
```

<!-- code_snippet_end -->

<!-- code_snippet_start:examples/imgui_reload/main/imgui_reload_main.cpp:r/Render loop/:r/imgui_port_render/+ -->

```cpp
    /* ---- Render loop ---- */
    while (true) {
        imgui_port_new_frame();
        app_ui_draw();
        imgui_port_render();
```

<!-- code_snippet_end -->

See [`API.md`](API.md) for the full public API reference.

## License

The ESP-IDF port code is licensed under Apache 2.0 (see [`LICENSE`](LICENSE)).
Dear ImGui is licensed under MIT (see [`imgui/LICENSE.txt`](imgui/LICENSE.txt)).
The bundled softraster library is licensed under MIT (copyright 2019 LAK132).
