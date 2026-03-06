# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: Apache-2.0
#
# Pytest for font_compress.py: builds the C++ binary_to_compressed_c tool,
# runs both the C++ and Python implementations on every bundled TTF font,
# and verifies they produce byte-identical compressed output.

import glob
import os
import re
import subprocess
import tempfile

import pytest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(REPO_ROOT, "imgui", "misc", "fonts")
CPP_SOURCE = os.path.join(FONT_DIR, "binary_to_compressed_c.cpp")
PY_SCRIPT = os.path.join(REPO_ROOT, "tools", "font_compress.py")

TTF_FILES = sorted(glob.glob(os.path.join(FONT_DIR, "*.ttf")))


@pytest.fixture(scope="session")
def cpp_tool(tmp_path_factory):
    """Build the C++ binary_to_compressed_c tool once per test session."""
    build_dir = tmp_path_factory.mktemp("cpp_build")
    binary = str(build_dir / "binary_to_compressed_c")
    subprocess.run(
        ["c++", "-o", binary, CPP_SOURCE],
        check=True,
    )
    return binary


def _extract_bytes(c_source: str) -> list[int]:
    """Extract the byte array from generated C source."""
    match = re.search(r"\{([^}]+)\}", c_source, re.DOTALL)
    assert match, "Could not find byte array in C source"
    return [int(n) for n in re.findall(r"\d+", match.group(1))]


@pytest.mark.parametrize(
    "ttf_path",
    TTF_FILES,
    ids=[os.path.basename(f) for f in TTF_FILES],
)
def test_compression_matches_cpp(ttf_path, cpp_tool, tmp_path):
    symbol = re.sub(r"[^a-zA-Z0-9_]", "_", os.path.splitext(os.path.basename(ttf_path))[0])

    # Run C++ tool
    cpp_result = subprocess.run(
        [cpp_tool, "-u8", ttf_path, symbol],
        capture_output=True, text=True, check=True,
    )
    cpp_bytes = _extract_bytes(cpp_result.stdout)

    # Run Python tool
    py_c = str(tmp_path / f"{symbol}.c")
    py_h = str(tmp_path / f"{symbol}.h")
    subprocess.run(
        ["python3", PY_SCRIPT, ttf_path, symbol, py_c, py_h],
        check=True,
    )
    with open(py_c) as f:
        py_bytes = _extract_bytes(f.read())

    assert len(py_bytes) == len(cpp_bytes), (
        f"Size mismatch: Python={len(py_bytes)}, C++={len(cpp_bytes)}"
    )
    assert py_bytes == cpp_bytes, "Compressed output differs between Python and C++"
