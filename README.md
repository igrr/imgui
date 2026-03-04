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

```c
#include "imgui_port_esp_lcd.h"
#include "imgui.h"

imgui_port_cfg_t cfg = {
    .panel_handle = panel,   // from esp_lcd_new_*()
    .width  = 800,
    .height = 480,
};
ESP_ERROR_CHECK(imgui_port_init(&cfg));

while (true) {
    imgui_port_new_frame();
    ImGui::ShowDemoWindow();
    imgui_port_render();
}
```

See [`API.md`](API.md) for the full public API reference.

## License

The ESP-IDF port code is licensed under Apache 2.0 (see [`LICENSE`](LICENSE)).
Dear ImGui is licensed under MIT (see [`imgui/LICENSE.txt`](imgui/LICENSE.txt)).
The bundled softraster library is licensed under MIT (copyright 2019 LAK132).
