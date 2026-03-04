# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: MIT

import pytest
import time
import subprocess

@pytest.mark.parametrize('target', ['esp32s3'])
@pytest.mark.parametrize('qemu_extra_args', ['-display sdl'])
def test_imgui(dut):
    dut.expect_exact('Display ImGui Demo', timeout=10)
    time.sleep(5)
    dut.qemu.take_screenshot('screenshot.pbm')
    subprocess.check_call(['magick', 'screenshot.pbm', 'screenshot.png'])
