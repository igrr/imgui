# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: Apache-2.0

"""
Performance benchmark test for the ImGui software renderer.

Runs the perf test app on the target, waits for the FPS counter
to stabilize (~5 seconds of sliding average), then extracts
and reports the FPS value.
"""

import re
import pytest


@pytest.mark.parametrize('target', ['esp32p4'])
def test_imgui_perf(dut):
    # Wait for the app to start
    dut.expect_exact('ImGui Perf Test Ready', timeout=30)

    # Collect FPS readings for ~7 seconds to let the sliding average stabilize.
    # The FPS counter prints once per second: "FPS: <value>"
    fps_pattern = re.compile(r'FPS: (\d+\.?\d*)')
    fps_values = []

    for _ in range(7):
        match = dut.expect(fps_pattern, timeout=5)
        fps = float(match.group(1))
        fps_values.append(fps)

    # Use the last reading as the stable value
    stable_fps = fps_values[-1]

    # Log all collected values for debugging
    print(f'FPS readings: {fps_values}')
    print(f'Stable FPS: {stable_fps}')

    # Basic sanity: FPS should be > 0
    assert stable_fps > 0, f'FPS too low: {stable_fps}'
