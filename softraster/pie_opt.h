/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 *
 * PIE (SIMD) optimized rendering functions for ESP32-P4.
 *
 * These functions provide vectorized implementations of inner-loop
 * operations used by the software rasterizer and the port layer.
 * They are compiled only when targeting ESP32-P4 (CONFIG_SOC_CPU_HAS_PIE).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill a buffer with a constant 32-bit word using 128-bit vector stores.
 * Used for framebuffer clear and opaque solid quad fill.
 *
 * @param dst   Destination buffer (should be 16-byte aligned for best perf)
 * @param word  32-bit value to fill with
 * @param count Number of 32-bit words to write
 */
void pie_fill_u32(uint32_t *dst, uint32_t word, int count);

/**
 * Alpha-blend a constant solid color onto a row of RGBA8888 pixels.
 * Implements: out[c] = (dst[c] * (255-alpha) + src[c] * alpha) >> 8
 * for R, G, B channels; alpha channel is set to 0xFF.
 *
 * @param dst       Destination pixel row (RGBA8888, modified in place)
 * @param count     Number of pixels
 * @param src_r     Source red component
 * @param src_g     Source green component
 * @param src_b     Source blue component
 * @param alpha     Source alpha (1..254; caller handles 0 and 255 cases)
 */
void pie_blend_solid_row(uint32_t *dst, int count,
                         uint8_t src_r, uint8_t src_g, uint8_t src_b,
                         uint8_t alpha);

/**
 * Swap R and B channels in-place for ABGR→ARGB conversion.
 * Processes 4 pixels at a time using PIE vector shifts and masks.
 *
 * @param buf       Pixel buffer (modified in place)
 * @param count     Number of pixels
 */
void pie_swap_rb(uint32_t *buf, int count);

#ifdef __cplusplus
}
#endif
