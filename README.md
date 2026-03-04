# ImGui ESP-IDF Component

[Dear ImGui](https://github.com/ocornut/imgui) v1.92.6 packaged as an ESP-IDF component, with a software-renderer backend for `esp_lcd` panels.

## Features

- Bundles the Dear ImGui library (upstream, unmodified)
- Software rasterizer: renders ImGui draw lists directly into a framebuffer — no GPU required
- `esp_lcd` backend: flushes the framebuffer via `esp_lcd_panel_draw_bitmap()`
- Supports 32-bit direct output (e.g. QEMU) and 16-bit RGB565 output with automatic conversion

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
