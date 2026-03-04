#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: Apache-2.0

"""
ImGui Hot Reload end-to-end test.

Boots the example in QEMU with networking, takes an initial screenshot,
modifies app_ui.cpp, rebuilds, uploads the new ELF, waits for reload,
and takes a post-reload screenshot.

Run:
    pytest pytest_imgui_reload.py -v -s
"""

import hashlib
import hmac as hmac_module
import subprocess
import time
import requests
import pytest
from pathlib import Path

PROJECT_DIR = Path(__file__).parent
DEVICE_PORT = 8080
APP_UI_SRC = PROJECT_DIR / "components" / "app_ui" / "app_ui.cpp"


def get_reloadable_elf_path(build_dir: Path | None = None) -> Path:
    if build_dir is None:
        build_dir = PROJECT_DIR / "build"
    return build_dir / "esp-idf" / "app_ui" / "app_ui_stripped.so"


def load_hmac_key(build_dir: Path | None = None) -> bytes | None:
    if build_dir is None:
        build_dir = PROJECT_DIR / "build"
    key_path = build_dir / "hotreload_hmac_key.bin"
    if key_path.exists():
        return key_path.read_bytes()
    return None


def upload_elf(port: int, build_dir: Path | None = None) -> requests.Response:
    url = f"http://127.0.0.1:{port}/upload"
    elf_path = get_reloadable_elf_path(build_dir)
    elf_data = elf_path.read_bytes()

    headers = {"Content-Type": "application/octet-stream"}
    hmac_key = load_hmac_key(build_dir)
    if hmac_key:
        headers["X-Hotreload-SHA256"] = hashlib.sha256(elf_data).hexdigest()
        headers["X-Hotreload-HMAC"] = hmac_module.new(
            hmac_key, elf_data, hashlib.sha256
        ).hexdigest()

    print(f"  Uploading {len(elf_data)} bytes to {url}")
    return requests.post(url, data=elf_data, headers=headers, timeout=30)


def rebuild(build_dir: Path | None = None):
    cmd = ["idf.py"]
    if build_dir is not None:
        cmd.extend(["-B", str(build_dir)])
    cmd.append("build")
    result = subprocess.run(
        cmd, cwd=PROJECT_DIR, capture_output=True, text=True, timeout=120
    )
    if result.returncode != 0:
        print(f"Build stdout:\n{result.stdout}")
        print(f"Build stderr:\n{result.stderr}")
        raise RuntimeError("Failed to rebuild")


@pytest.fixture
def original_code():
    """Save and restore app_ui.cpp around the test."""
    original = APP_UI_SRC.read_text()
    yield original
    APP_UI_SRC.write_text(original)
    rebuild()


@pytest.mark.parametrize("target", ["esp32c3"])
@pytest.mark.parametrize(
    "qemu_extra_args",
    [
        f"-display sdl -nic user,model=open_eth,id=lo0,"
        f"hostfwd=tcp:127.0.0.1:{{host_port}}-:{DEVICE_PORT}"
    ],
    indirect=True,
)
def test_imgui_reload(dut, original_code, qemu_host_port):
    host_port = qemu_host_port

    # 1. Wait for app ready
    print("Step 1: Waiting for app to be ready...")
    dut.expect_exact("ImGui Hot Reload Ready", timeout=30)
    time.sleep(2)

    # 2. Take initial screenshot
    print("Step 2: Taking initial screenshot...")
    dut.qemu.take_screenshot("screenshot_before.pbm")
    subprocess.check_call(["magick", "screenshot_before.pbm", "screenshot_before.png"])

    # 3. Modify app_ui.cpp
    print("Step 3: Modifying app_ui.cpp...")
    content = APP_UI_SRC.read_text()
    APP_UI_SRC.write_text(content.replace("Hello from app_ui!", "Goodbye from app_ui!"))

    # 4. Rebuild
    print("Step 4: Rebuilding...")
    rebuild()

    # 5. Upload new ELF
    print("Step 5: Uploading new ELF...")
    response = upload_elf(host_port)
    print(f"  Server response: {response.status_code} - {response.text.strip()}")
    assert response.status_code == 200, f"Upload failed: {response.text}"

    # 6. Wait for reload
    print("Step 6: Waiting for reload...")
    dut.expect("Reload complete", timeout=30)
    time.sleep(2)

    # 7. Take post-reload screenshot
    print("Step 7: Taking post-reload screenshot...")
    dut.qemu.take_screenshot("screenshot_after.pbm")
    subprocess.check_call(["magick", "screenshot_after.pbm", "screenshot_after.png"])

    print("=== ImGui Hot Reload Test PASSED ===")
