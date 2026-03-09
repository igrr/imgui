# API Reference

## Header files

- [imgui_port_esp_lcd.h](#file-imgui_port_esp_lcdh)

## File imgui_port_esp_lcd.h

## Structures and Types

| Type | Name |
| ---: | :--- |
| struct | [**imgui\_port\_cfg\_t**](#struct-imgui_port_cfg_t) <br>_Configuration for imgui esp\_lcd port._ |
| typedef struct imgui\_port\_renderer\_t \* | [**imgui\_port\_renderer\_handle\_t**](#typedef-imgui_port_renderer_handle_t)  <br>_Opaque renderer handle._ |

## Functions

| Type | Name |
| ---: | :--- |
|  void | [**imgui\_port\_deinit**](#function-imgui_port_deinit) (void) <br>_Destroy the imgui context and free internally-allocated resources._ |
|  void | [**imgui\_port\_delete\_renderer**](#function-imgui_port_delete_renderer) (imgui\_port\_renderer\_handle\_t handle) <br>_Delete a renderer previously created by a factory function._ |
|  void | [**imgui\_port\_enable\_fps\_counter\_console**](#function-imgui_port_enable_fps_counter_console) (void) <br>_Enable FPS logging to the console._ |
|  void | [**imgui\_port\_enable\_fps\_counter\_ui**](#function-imgui_port_enable_fps_counter_ui) (void) <br>_Enable an on-screen FPS counter overlay._ |
|  void | [**imgui\_port\_enable\_profiling\_console**](#function-imgui_port_enable_profiling_console) (void) <br>_Enable rendering profiling output to the console._ |
|  esp\_err\_t | [**imgui\_port\_init**](#function-imgui_port_init) (const [**imgui\_port\_cfg\_t**](#struct-imgui_port_cfg_t)\* cfg) <br>_Initialize imgui with an esp\_lcd panel backend._ |
|  void | [**imgui\_port\_new\_frame**](#function-imgui_port_new_frame) (void) <br>_Begin a new ImGui frame._ |
|  esp\_err\_t | [**imgui\_port\_new\_renderer\_abgr8888**](#function-imgui_port_new_renderer_abgr8888) (imgui\_port\_renderer\_handle\_t \* out\_handle) <br>_Create a renderer for ABGR8888 output (32 bpp, R at byte-0 on LE)._ |
|  esp\_err\_t | [**imgui\_port\_new\_renderer\_argb8888**](#function-imgui_port_new_renderer_argb8888) (imgui\_port\_renderer\_handle\_t \* out\_handle) <br>_Create a renderer for ARGB8888 output (32 bpp, B at byte-0 on LE)._ |
|  esp\_err\_t | [**imgui\_port\_new\_renderer\_rgb565**](#function-imgui_port_new_renderer_rgb565) (imgui\_port\_renderer\_handle\_t \* out\_handle) <br>_Create a renderer for RGB565 output (16 bpp)._ |
|  esp\_err\_t | [**imgui\_port\_new\_renderer\_rgb888**](#function-imgui_port_new_renderer_rgb888) (imgui\_port\_renderer\_handle\_t \* out\_handle) <br>_Create a renderer for RGB888 output (24 bpp, R at byte-0)._ |
|  void | [**imgui\_port\_render**](#function-imgui_port_render) (void) <br>_Render the current ImGui frame to the LCD panel._ |


## Structures and Types Documentation

### struct `imgui_port_cfg_t`

_Configuration for imgui esp\_lcd port._
Variables:

-  int height  <br>Display height in pixels

-  esp\_lcd\_panel\_handle\_t panel_handle  <br>Initialized LCD panel handle

-  void \* render_buf  <br>_Optional external render buffer (array of width\*height RGBA8888 pixels)._<br>If NULL the port allocates one internally, preferring PSRAM. Supply an external buffer when you want to avoid a heap allocation – for example by using the QEMU panel's dedicated frame buffer returned by esp\_lcd\_rgb\_qemu\_get\_frame\_buffer().

-  imgui\_port\_renderer\_handle\_t renderer  <br>_Renderer handle defining the output pixel format._<br>Must be created by one of the imgui\_port\_new\_renderer\_\*() functions before calling imgui\_port\_init(). The port does NOT take ownership – call imgui\_port\_delete\_renderer() after imgui\_port\_deinit() if desired.

-  int width  <br>Display width in pixels

### typedef `imgui_port_renderer_handle_t`

_Opaque renderer handle._
```c
typedef struct imgui_port_renderer_t* imgui_port_renderer_handle_t;
```

Created by one of the imgui\_port\_new\_renderer\_\*() factory functions. Encapsulates the output pixel format and conversion logic. Only the factory function that is actually called (and its conversion routine) will be linked into the final binary – unused formats are eliminated by the linker's gc-sections pass.

## Functions Documentation

### function `imgui_port_deinit`

_Destroy the imgui context and free internally-allocated resources._
```c
void imgui_port_deinit (
    void
) 
```

Does NOT free an externally-supplied render\_buf.
### function `imgui_port_delete_renderer`

_Delete a renderer previously created by a factory function._
```c
void imgui_port_delete_renderer (
    imgui_port_renderer_handle_t handle
) 
```

### function `imgui_port_enable_fps_counter_console`

_Enable FPS logging to the console._
```c
void imgui_port_enable_fps_counter_console (
    void
) 
```

Prints the current frame rate via ESP\_LOGI once per second. Call this once after imgui\_port\_init().
### function `imgui_port_enable_fps_counter_ui`

_Enable an on-screen FPS counter overlay._
```c
void imgui_port_enable_fps_counter_ui (
    void
) 
```

Draws a small translucent window in the lower-right corner of the display showing the current frame rate. Call this once after imgui\_port\_init().
### function `imgui_port_enable_profiling_console`

_Enable rendering profiling output to the console._
```c
void imgui_port_enable_profiling_console (
    void
) 
```

Prints a per-stage timing breakdown once per second, showing the average time (in microseconds) spent in each rendering stage:


* new\_frame: ImGui::NewFrame() (via linker wrapping)
* render: ImGui::Render() (via linker wrapping)
* clear: framebuffer clear
* rasterize: software rasterization (sw\_render)
* convert: pixel format conversion
* flush: esp\_lcd\_panel\_draw\_bitmap()
* total: full frame time

Requires CONFIG\_IMGUI\_PROFILING=y. If profiling is not enabled, logs a warning and does nothing.
### function `imgui_port_init`

_Initialize imgui with an esp\_lcd panel backend._
```c
esp_err_t imgui_port_init (
    const imgui_port_cfg_t * cfg
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
### function `imgui_port_new_renderer_abgr8888`

_Create a renderer for ABGR8888 output (32 bpp, R at byte-0 on LE)._
```c
esp_err_t imgui_port_new_renderer_abgr8888 (
    imgui_port_renderer_handle_t * out_handle
) 
```

No conversion – the internal render buffer is already in this format.
### function `imgui_port_new_renderer_argb8888`

_Create a renderer for ARGB8888 output (32 bpp, B at byte-0 on LE)._
```c
esp_err_t imgui_port_new_renderer_argb8888 (
    imgui_port_renderer_handle_t * out_handle
) 
```

Swaps R and B channels relative to the internal RGBA8888 render buffer. Suitable for displays that expect BGRA byte order (e.g. QEMU SDL in BPP\_32 mode, many DRM/KMS framebuffers).
### function `imgui_port_new_renderer_rgb565`

_Create a renderer for RGB565 output (16 bpp)._
```c
esp_err_t imgui_port_new_renderer_rgb565 (
    imgui_port_renderer_handle_t * out_handle
) 
```

### function `imgui_port_new_renderer_rgb888`

_Create a renderer for RGB888 output (24 bpp, R at byte-0)._
```c
esp_err_t imgui_port_new_renderer_rgb888 (
    imgui_port_renderer_handle_t * out_handle
) 
```

Strips the alpha channel from the internal RGBA8888 buffer.
### function `imgui_port_render`

_Render the current ImGui frame to the LCD panel._
```c
void imgui_port_render (
    void
) 
```

Calls ImGui::Render(), rasterizes the draw lists into an RGBA8888 framebuffer using a software renderer, optionally converts to RGB565, and flushes to the panel via esp\_lcd\_panel\_draw\_bitmap().


