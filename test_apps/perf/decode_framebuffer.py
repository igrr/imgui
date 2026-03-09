#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: Apache-2.0

"""
Decode a base64-encoded framebuffer dump from ESP32 console output.

Usage:
    # From a log file:
    python decode_framebuffer.py monitor_output.txt -o screenshot.png

    # From stdin (pipe from idf.py monitor):
    idf.py monitor | python decode_framebuffer.py - -o screenshot.png

    # From clipboard (copy-paste from terminal):
    pbpaste | python decode_framebuffer.py - -o screenshot.png

The script looks for FRAMEBUFFER_BEGIN / FRAMEBUFFER_END markers in the
input and decodes the base64 data between them into a PNG image.
"""

import argparse
import base64
import os
import re
import sys

try:
    from PIL import Image
except ImportError:
    Image = None

# Pixel format strings for each bpp value
_BPP_TO_MODE = {
    32: 'RGBA',
    24: 'RGB',
    16: 'RGB',  # RGB565 needs special handling
}


def decode_framebuffer(input_text):
    """Extract and decode a base64 framebuffer dump from console output.

    Args:
        input_text: String containing the full console output with
                    FRAMEBUFFER_BEGIN/END markers.

    Returns:
        Tuple of (width, height, bpp, raw_bytes) or None if no dump found.
    """
    begin_pattern = re.compile(r'FRAMEBUFFER_BEGIN\s+(\d+)\s+(\d+)\s+(\d+)')
    lines = input_text.splitlines()

    width = height = bpp = 0
    b64_chunks = []
    capturing = False

    for line in lines:
        if not capturing:
            m = begin_pattern.search(line)
            if m:
                width = int(m.group(1))
                height = int(m.group(2))
                bpp = int(m.group(3))
                capturing = True
                b64_chunks = []
            continue

        if 'FRAMEBUFFER_END' in line:
            break

        stripped = line.strip()
        if not stripped:
            continue
        # Strip common prefixes:
        # - pytest-embedded timestamps: "2026-03-09 11:36:16 "
        # - ESP log prefixes: "I (12345) tag: "
        cleaned = re.sub(r'^\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\s+', '', stripped)
        cleaned = re.sub(r'^[IWEDV]\s+\(\d+\)\s+\S+:\s+', '', cleaned)
        # After stripping prefixes, the remainder should be pure base64
        # Validate that it looks like base64 (only base64 chars)
        if re.fullmatch(r'[A-Za-z0-9+/=]+', cleaned):
            b64_chunks.append(cleaned)

    if not b64_chunks:
        return None

    b64_str = ''.join(b64_chunks)
    # Fix padding if needed
    b64_str += '=' * (-len(b64_str) % 4)
    raw_data = base64.b64decode(b64_str)
    expected_size = width * height * (bpp // 8)
    if len(raw_data) != expected_size:
        print(f'Warning: expected {expected_size} bytes, got {len(raw_data)}',
              file=sys.stderr)

    return width, height, bpp, raw_data


def save_as_png(width, height, bpp, raw_data, output_path):
    """Save raw pixel data as a PNG file.

    Args:
        width: Image width
        height: Image height
        bpp: Bits per pixel (16, 24, or 32)
        raw_data: Raw pixel bytes
        output_path: Path to write the PNG file
    """
    if Image is None:
        print('Error: Pillow is required. Install with: pip install Pillow',
              file=sys.stderr)
        sys.exit(1)

    mode = _BPP_TO_MODE.get(bpp)
    if mode is None:
        print(f'Error: unsupported bpp={bpp}', file=sys.stderr)
        sys.exit(1)

    if bpp == 16:
        # RGB565 → RGB888 conversion
        import struct
        pixels = []
        for i in range(0, len(raw_data), 2):
            pixel = struct.unpack('<H', raw_data[i:i+2])[0]
            r = ((pixel >> 11) & 0x1F) << 3
            g = ((pixel >> 5) & 0x3F) << 2
            b = (pixel & 0x1F) << 3
            pixels.extend([r, g, b])
        raw_data = bytes(pixels)

    img = Image.frombytes(mode, (width, height), raw_data)
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    img.save(output_path)
    print(f'Saved {output_path} ({width}x{height}, {bpp}bpp)')


def main():
    parser = argparse.ArgumentParser(
        description='Decode base64 framebuffer dump from ESP32 console output')
    parser.add_argument('input', help='Input file path, or "-" for stdin')
    parser.add_argument('-o', '--output', default='screenshot.png',
                        help='Output PNG file path (default: screenshot.png)')
    args = parser.parse_args()

    if args.input == '-':
        text = sys.stdin.read()
    else:
        with open(args.input) as f:
            text = f.read()

    result = decode_framebuffer(text)
    if result is None:
        print('Error: no FRAMEBUFFER_BEGIN/END markers found in input',
              file=sys.stderr)
        sys.exit(1)

    width, height, bpp, raw_data = result
    save_as_png(width, height, bpp, raw_data, args.output)


if __name__ == '__main__':
    main()
