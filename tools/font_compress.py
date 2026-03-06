#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
# SPDX-License-Identifier: Apache-2.0
#
# Python port of imgui's misc/fonts/binary_to_compressed_c.cpp.
# Converts a binary file (typically a .ttf font) into a C source file
# containing the data compressed with the STB compression format that
# ImGui can decompress with AddFontFromMemoryCompressedTTF().
#
# Usage:
#   python font_compress.py <input_file> <symbol_name> <output_file>

import argparse
import os
import struct
import sys


# ---------------------------------------------------------------------------
# STB compression (ported from stb.h via binary_to_compressed_c.cpp)
# ---------------------------------------------------------------------------

_WINDOW = 0x40000  # 256K
_HASH_SIZE = 32768


def _adler32(adler: int, data: bytes) -> int:
    ADLER_MOD = 65521
    s1 = adler & 0xFFFF
    s2 = (adler >> 16) & 0xFFFF
    idx = 0
    length = len(data)
    while length > 0:
        blocklen = min(length, 5552)
        for _ in range(blocklen):
            s1 += data[idx]
            s2 += s1
            idx += 1
        s1 %= ADLER_MOD
        s2 %= ADLER_MOD
        length -= blocklen
    return ((s2 & 0xFFFF) << 16) | (s1 & 0xFFFF)


def _matchlen(data: bytes, m1: int, m2: int, maxlen: int) -> int:
    i = 0
    while i < maxlen and m1 + i < len(data) and m2 + i < len(data):
        if data[m1 + i] != data[m2 + i]:
            return i
        i += 1
    return i


def _not_junk(best: int, dist: int) -> bool:
    return ((best > 2 and dist <= 0x00100)
            or (best > 5 and dist <= 0x04000)
            or (best > 7 and dist <= 0x80000))


def _compress_chunk(data: bytes, start: int, end: int, length: int,
                    pending_literals: int, chash: list, mask: int) -> tuple:
    out = bytearray()
    window = _WINDOW
    lit_start = start - pending_literals
    q = start

    def _scramble(h):
        return ((h + (h >> 16)) & mask) & 0xFFFFFFFF

    def _hc3(c, d, e):
        return ((data[c] << 14) + (data[d] << 7) + data[e]) & 0xFFFFFFFF

    def _hc2(h, c, d):
        return (((h << 14) + (h >> 18) + (data[c] << 7) + data[d])) & 0xFFFFFFFF

    def _out1(v):
        out.append(v & 0xFF)

    def _out2(v):
        out.append((v >> 8) & 0xFF)
        out.append(v & 0xFF)

    def _out3(v):
        out.append((v >> 16) & 0xFF)
        out.append((v >> 8) & 0xFF)
        out.append(v & 0xFF)

    def _outliterals(start_pos, numlit):
        pos = start_pos
        remaining = numlit
        while remaining > 65536:
            _outliterals(pos, 65536)
            pos += 65536
            remaining -= 65536
        if remaining == 0:
            pass
        elif remaining <= 32:
            _out1(0x20 + remaining - 1)
        elif remaining <= 2048:
            _out2(0x0800 + remaining - 1)
        else:
            _out3(0x070000 + remaining - 1)
        out.extend(data[pos:pos + remaining])

    while q < start + length and q + 12 < end:
        best = 2
        dist = 0

        if q + 65536 > end:
            match_max = end - q
        else:
            match_max = 65536

        h = _hc3(q, q + 1, q + 2)
        h1 = _scramble(h)
        t = chash[h1]
        if t is not None:
            m = _matchlen(data, t, q, match_max)
            d = q - t
            if m > best and d <= window and (m > 9 or _not_junk(m, d)):
                best = m
                dist = d

        h = _hc2(h, q + 3, q + 4)
        h2 = _scramble(h)
        h = _hc2(h, q + 5, q + 6)
        t = chash[h2]
        if t is not None:
            d = q - t
            if d != dist:
                m = _matchlen(data, t, q, match_max)
                if m > best and d <= window and (m > 9 or _not_junk(m, d)):
                    best = m
                    dist = d

        h = _hc2(h, q + 7, q + 8)
        h3 = _scramble(h)
        h = _hc2(h, q + 9, q + 10)
        t = chash[h3]
        if t is not None:
            d = q - t
            if d != dist:
                m = _matchlen(data, t, q, match_max)
                if m > best and d <= window and (m > 9 or _not_junk(m, d)):
                    best = m
                    dist = d

        h = _hc2(h, q + 11, q + 12)
        h4 = _scramble(h)
        t = chash[h4]
        if t is not None:
            d = q - t
            if d != dist:
                m = _matchlen(data, t, q, match_max)
                if m > best and d <= window and (m > 9 or _not_junk(m, d)):
                    best = m
                    dist = d

        chash[h1] = chash[h2] = chash[h3] = chash[h4] = q

        if best < 3:
            q += 1
        elif best > 2 and best <= 0x80 and dist <= 0x100:
            _outliterals(lit_start, q - lit_start)
            lit_start = q + best
            q += best
            _out1(0x80 + best - 1)
            _out1(dist - 1)
        elif best > 5 and best <= 0x100 and dist <= 0x4000:
            _outliterals(lit_start, q - lit_start)
            lit_start = q + best
            q += best
            _out2(0x4000 + dist - 1)
            _out1(best - 1)
        elif best > 7 and best <= 0x100 and dist <= 0x80000:
            _outliterals(lit_start, q - lit_start)
            lit_start = q + best
            q += best
            _out3(0x180000 + dist - 1)
            _out1(best - 1)
        elif best > 8 and best <= 0x10000 and dist <= 0x80000:
            _outliterals(lit_start, q - lit_start)
            lit_start = q + best
            q += best
            _out3(0x100000 + dist - 1)
            _out2(best - 1)
        elif best > 9 and dist <= 0x1000000:
            if best > 65536:
                best = 65536
            _outliterals(lit_start, q - lit_start)
            lit_start = q + best
            q += best
            if best <= 0x100:
                _out1(0x06)
                _out3(dist - 1)
                _out1(best - 1)
            else:
                _out1(0x04)
                _out3(dist - 1)
                _out2(best - 1)
        else:
            q += 1

    if q - start < length:
        q = start + length

    pending_literals = q - lit_start

    return out, pending_literals, q


def stb_compress(data: bytes) -> bytes:
    length = len(data)
    out = bytearray()

    # Stream signature
    out.append(0x57)
    out.append(0xBC)
    out.append(0x00)
    out.append(0x00)

    # 64-bit length (leading 0 + actual length)
    out.extend(struct.pack('>I', 0))
    out.extend(struct.pack('>I', length))

    # Window size
    out.extend(struct.pack('>I', _WINDOW))

    chash = [None] * _HASH_SIZE
    mask = _HASH_SIZE - 1

    chunk_out, pending_literals, _ = _compress_chunk(
        data, 0, length, length, 0, chash, mask)
    out.extend(chunk_out)

    # Flush remaining literals
    remaining = pending_literals
    lit_start = length - remaining
    while remaining > 65536:
        count = 65536
        out.extend(struct.pack('>I', 0x070000 + count - 1)[1:])  # 3 bytes
        out.extend(data[lit_start:lit_start + count])
        lit_start += count
        remaining -= count
    if remaining > 0:
        if remaining <= 32:
            out.append(0x20 + remaining - 1)
        elif remaining <= 2048:
            v = 0x0800 + remaining - 1
            out.append((v >> 8) & 0xFF)
            out.append(v & 0xFF)
        else:
            v = 0x070000 + remaining - 1
            out.append((v >> 16) & 0xFF)
            out.append((v >> 8) & 0xFF)
            out.append(v & 0xFF)
        out.extend(data[lit_start:lit_start + remaining])

    # End opcode
    out.append(0x05)
    out.append(0xFA)

    # Adler32 checksum
    running_adler = _adler32(1, data)
    out.extend(struct.pack('>I', running_adler))

    return bytes(out)


# ---------------------------------------------------------------------------
# C source generation
# ---------------------------------------------------------------------------

def generate_c_source(input_path: str, symbol: str, compressed: bytes,
                      original_size: int) -> str:
    lines = []
    basename = os.path.basename(input_path)
    lines.append(f"// File: '{basename}' ({original_size} bytes)")
    lines.append(f"// Compressed with tools/font_compress.py")
    lines.append(f"")
    lines.append(f"const unsigned int {symbol}_compressed_size = {len(compressed)};")
    lines.append(f"const unsigned char {symbol}_compressed_data[{len(compressed)}] =")
    lines.append("{")

    column = 0
    row_parts = []
    for b in compressed:
        part = f"{b},"
        column += len(part)
        row_parts.append(part)
        if column >= 180:
            lines.append("    " + "".join(row_parts))
            row_parts = []
            column = 0
    if row_parts:
        lines.append("    " + "".join(row_parts))

    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def generate_header(symbol: str) -> str:
    guard = f"{symbol.upper()}_H"
    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        f"",
        f"extern const unsigned int {symbol}_compressed_size;",
        f"extern const unsigned char {symbol}_compressed_data[];",
        f"",
        f"#endif // {guard}",
        f"",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Convert a binary file to a compressed C array for ImGui fonts")
    parser.add_argument("input", help="Input file (e.g. myfont.ttf)")
    parser.add_argument("symbol", help="C symbol name prefix")
    parser.add_argument("output_source", help="Output .c file path")
    parser.add_argument("output_header", help="Output .h file path")
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        data = f.read()

    # Pad to 4-byte boundary for compression
    padded = data + b'\x00' * ((4 - len(data) % 4) % 4)
    compressed = stb_compress(padded)

    source = generate_c_source(args.input, args.symbol, compressed, len(data))
    with open(args.output_source, "w") as f:
        f.write(source)

    header = generate_header(args.symbol)
    with open(args.output_header, "w") as f:
        f.write(header)


if __name__ == "__main__":
    main()
