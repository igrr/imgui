# QEMU Test App


This runs allows for running graphics-related tests in QEMU.

See pytest_imgui.py for the pytest part.

To run:
```
idf.py build
pytest --target esp32s3  pytest_imgui.py
```

The screenshot will be captured from QEMU virtual display and stored in screenshot.png.
