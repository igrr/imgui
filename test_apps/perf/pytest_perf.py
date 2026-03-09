# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: Apache-2.0

"""
Performance benchmark test for the ImGui software renderer.

Runs the perf test app on the target, waits for the FPS counter
to stabilize, then extracts and reports the FPS value.

Also captures the rendered framebuffer (dumped as base64 by the
esp_lcd_screenshot driver) and compares it against a golden
reference PNG to detect rendering regressions.
"""

import os
import re

import pytest

from decode_framebuffer import decode_framebuffer, save_as_png

try:
    from PIL import Image
except ImportError:
    Image = None

GOLDEN_DIR = os.path.join(os.path.dirname(__file__), 'golden')
GOLDEN_PNG = os.path.join(GOLDEN_DIR, 'reference.png')


def _to_str(val):
    """Convert bytes or str to str."""
    return val.decode('utf-8', errors='replace') if isinstance(val, bytes) else val


def _capture_framebuffer_text(dut):
    """Capture the raw text of the base64 framebuffer dump from serial output."""
    captured_lines = []

    # Wait for FRAMEBUFFER_BEGIN marker
    begin_match = dut.expect(re.compile(r'(FRAMEBUFFER_BEGIN\s+\d+\s+\d+\s+\d+)'), timeout=30)
    captured_lines.append(_to_str(begin_match.group(1)))

    # Collect lines until FRAMEBUFFER_END
    while True:
        try:
            line_match = dut.expect(re.compile(r'(.+)'), timeout=30)
            text = _to_str(line_match.group(1))
            captured_lines.append(text)
            if 'FRAMEBUFFER_END' in text:
                break
        except Exception:
            break

    return '\n'.join(captured_lines)


def _compare_golden(width, height, bpp, raw_data):
    """Compare rendered framebuffer against golden reference PNG."""
    if Image is None:
        pytest.fail('Pillow is required for golden image comparison')

    mode = 'RGBA' if bpp == 32 else 'RGB'

    if not os.path.exists(GOLDEN_PNG):
        save_as_png(width, height, bpp, raw_data, GOLDEN_PNG)
        print(f'Golden reference created: {GOLDEN_PNG}')
        print('Check this file into the repository.')
        return

    ref_img = Image.open(GOLDEN_PNG).convert(mode)
    assert ref_img.size == (width, height), \
        f'Golden image size {ref_img.size} != rendered size ({width}, {height})'

    ref_data = ref_img.tobytes()
    if ref_data == raw_data:
        print('Golden image comparison: MATCH')
        return

    # Save the actual output for debugging
    actual_png = os.path.join(GOLDEN_DIR, 'actual.png')
    save_as_png(width, height, bpp, raw_data, actual_png)

    # Count differing pixels
    n_pixels = width * height
    bytes_per_pixel = bpp // 8
    diff_count = 0
    for i in range(n_pixels):
        off = i * bytes_per_pixel
        if ref_data[off:off + bytes_per_pixel] != raw_data[off:off + bytes_per_pixel]:
            diff_count += 1

    pytest.fail(
        f'Golden image mismatch: {diff_count}/{n_pixels} pixels differ. '
        f'Actual output saved to {actual_png}'
    )


@pytest.mark.parametrize('target', ['esp32p4'])
def test_imgui_perf(dut):
    # Wait for the app to start
    dut.expect_exact('ImGui Perf Test Ready', timeout=30)

    # Capture the framebuffer dump (happens after 5 warm-up frames)
    fb_text = _capture_framebuffer_text(dut)
    fb_result = decode_framebuffer(fb_text)

    # Collect FPS readings for ~7 seconds to let the sliding average stabilize.
    fps_pattern = re.compile(r'FPS: (\d+\.?\d*)')
    fps_values = []

    for _ in range(7):
        match = dut.expect(fps_pattern, timeout=5)
        fps = float(_to_str(match.group(1)))
        fps_values.append(fps)

    stable_fps = fps_values[-1]
    print(f'FPS readings: {fps_values}')
    print(f'Stable FPS: {stable_fps}')
    assert stable_fps > 0, f'FPS too low: {stable_fps}'

    # Collect profiling data (per-stage timing breakdown in microseconds).
    # The profiling output is printed once per second by the port layer.
    prof_pattern = re.compile(
        r'IMGUI_PROF: new_frame=(\d+) render=(\d+) clear=(\d+) '
        r'rasterize=(\d+) convert=(\d+) flush=(\d+) total=(\d+) frames=(\d+)'
    )
    prof_match = dut.expect(prof_pattern, timeout=5)
    prof = {
        'new_frame': int(_to_str(prof_match.group(1))),
        'render': int(_to_str(prof_match.group(2))),
        'clear': int(_to_str(prof_match.group(3))),
        'rasterize': int(_to_str(prof_match.group(4))),
        'convert': int(_to_str(prof_match.group(5))),
        'flush': int(_to_str(prof_match.group(6))),
        'total': int(_to_str(prof_match.group(7))),
        'frames': int(_to_str(prof_match.group(8))),
    }
    print(f'Profiling breakdown (avg us/frame): {prof}')
    for stage in ('new_frame', 'render', 'clear', 'rasterize', 'convert', 'flush'):
        if prof['total'] > 0:
            pct = 100.0 * prof[stage] / prof['total']
            print(f'  {stage:12s}: {prof[stage]:6d} us ({pct:5.1f}%)')
    print(f'  {"total":12s}: {prof["total"]:6d} us')

    # Golden image comparison
    if fb_result is not None:
        width, height, bpp, raw_data = fb_result
        _compare_golden(width, height, bpp, raw_data)
    else:
        pytest.fail('Framebuffer capture failed: no FRAMEBUFFER_BEGIN/END markers found')
