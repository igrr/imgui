# API Reference

## Header files

- [imgui_port_esp_lcd.h](#file-imgui_port_esp_lcdh)

## File imgui_port_esp_lcd.h





## Structures and Types

| Type | Name |
| ---: | :--- |
| struct | [**imgui\_port\_cfg\_t**](#struct-imgui_port_cfg_t) <br>_Configuration for imgui esp\_lcd port._ |

## Functions

| Type | Name |
| ---: | :--- |
|  void | [**imgui\_port\_deinit**](#function-imgui_port_deinit) (void) <br>_Destroy the imgui context and free internally-allocated resources._ |
|  esp\_err\_t | [**imgui\_port\_init**](#function-imgui_port_init) (const [**imgui\_port\_cfg\_t**](#struct-imgui_port_cfg_t) \*cfg) <br>_Initialize imgui with an esp\_lcd panel backend._ |
|  void | [**imgui\_port\_new\_frame**](#function-imgui_port_new_frame) (void) <br>_Begin a new ImGui frame._ |
|  void | [**imgui\_port\_render**](#function-imgui_port_render) (void) <br>_Render the current ImGui frame to the LCD panel._ |


## Structures and Types Documentation

### struct `imgui_port_cfg_t`

_Configuration for imgui esp\_lcd port._

Variables:

-  bool direct_output  <br>_When true, the render buffer is passed to esp\_lcd\_panel\_draw\_bitmap as-is (no RGBA→RGB565 conversion). Use this when the panel accepts 32-bit pixels (e.g. QEMU RGB panel in BPP\_32 mode). When false (default), an RGB565 conversion is performed first._

-  int height  <br>Display height in pixels

-  esp\_lcd\_panel\_handle\_t panel_handle  <br>Initialized LCD panel handle

-  void \* render_buf  <br>_Optional external render buffer (array of width\*height color32\_t = RGBA8888)._<br>If NULL the port allocates one internally, preferring PSRAM. Supply an external buffer when you want to avoid a heap allocation – for example by using the QEMU panel's dedicated frame buffer returned by esp\_lcd\_rgb\_qemu\_get\_frame\_buffer().

-  bool swap_rb  <br>_When true, swap the Red and Blue channels before output._<br>The software renderer produces RGBA8888 (R at byte-0). Some display controllers or host systems (e.g. QEMU's SDL window in BPP\_32 mode) expect BGRA8888 (B at byte-0). Set this to true in that case. Applies to both direct\_output and RGB565 conversion paths.

-  int width  <br>Display width in pixels


## Functions Documentation

### function `imgui_port_deinit`

_Destroy the imgui context and free internally-allocated resources._
```c
void imgui_port_deinit (
    void
) 
```


Does NOT free an externally-supplied render\_buf.
### function `imgui_port_init`

_Initialize imgui with an esp\_lcd panel backend._
```c
esp_err_t imgui_port_init (
    const imgui_port_cfg_t *cfg
) 
```


Builds the ImGui context and font atlas, and prepares the software renderer.



**Parameters:**


* `cfg` Port configuration 


**Returns:**

ESP\_OK on success, ESP\_ERR\_NO\_MEM if allocation fails
### function `imgui_port_new_frame`

_Begin a new ImGui frame._
```c
void imgui_port_new_frame (
    void
) 
```


Call this before any ImGui:: widget calls. Updates the delta time.
### function `imgui_port_render`

_Render the current ImGui frame to the LCD panel._
```c
void imgui_port_render (
    void
) 
```


Calls ImGui::Render(), rasterizes the draw lists into an RGBA8888 framebuffer using a software renderer, optionally converts to RGB565, and flushes to the panel via esp\_lcd\_panel\_draw\_bitmap().


