/*
 * SPDX-FileCopyrightText: 2026 Ivan Grokhotkov
 * SPDX-License-Identifier: Apache-2.0
 *
 * PIE (SIMD) optimized rendering primitives for ESP32-P4.
 *
 * ESP32-P4 PIE provides 8 x 128-bit vector registers (q0-q7) that can
 * operate on 16 x 8-bit, 8 x 16-bit, or 4 x 32-bit elements.
 * Vector loads/stores move 128 bits (16 bytes = 4 RGBA pixels) at a time.
 *
 * IMPORTANT: PIE load/store instructions silently force 16-byte alignment
 * (lower address bits are cleared).  All functions must handle unaligned
 * head/tail pixels with scalar code before entering the PIE loop.
 */

#include "pie_opt.h"

/*
 * Compute how many leading pixels to process with scalar code so that the
 * pointer becomes 16-byte aligned for PIE loads/stores.
 * Returns 0 if already aligned, up to 3 otherwise (since each pixel is 4 bytes).
 */
static inline int align_head(const uint32_t *p)
{
    uintptr_t addr = (uintptr_t)p;
    uintptr_t misalign = addr & 0xFU;  /* bytes past 16-byte boundary */
    if (misalign == 0) return 0;
    return (int)((16 - misalign) / 4);  /* pixels to reach alignment */
}

/* -------------------------------------------------------------------------- */
/*  pie_fill_u32 — vectorized constant fill                                    */
/* -------------------------------------------------------------------------- */

void pie_fill_u32(uint32_t *dst, uint32_t word, int count)
{
    uint32_t *p = dst;
    int rem = count;

    /* Scalar head to reach 16-byte alignment */
    int head = align_head(p);
    if (head > rem) head = rem;
    for (int i = 0; i < head; i++) {
        *p++ = word;
    }
    rem -= head;

    /* Broadcast the 32-bit word to all 4 lanes of q0 */
    __asm__ volatile("esp.vldbc.32.ip q0, %0, 0" : : "r"(&word) : "memory");

    /* Store 16 pixels (4 x 128 bits) per iteration, unrolled to reduce
     * loop branch overhead on large fills (e.g. framebuffer clear). */
    int n16 = rem >> 4;  /* groups of 16 pixels */
    for (int i = 0; i < n16; i++) {
        __asm__ volatile(
            "esp.vst.128.ip q0, %0, 16 \n"
            "esp.vst.128.ip q0, %0, 16 \n"
            "esp.vst.128.ip q0, %0, 16 \n"
            "esp.vst.128.ip q0, %0, 16 \n"
            : "+r"(p) : : "memory"
        );
    }

    /* Handle remaining 1-15 pixels: up to 3 vector stores + scalar tail */
    int mid = (rem >> 2) & 3;  /* 0-3 remaining 4-pixel groups */
    for (int i = 0; i < mid; i++) {
        __asm__ volatile("esp.vst.128.ip q0, %0, 16" : "+r"(p) : : "memory");
    }

    int tail = rem & 3;
    for (int i = 0; i < tail; i++) {
        p[i] = word;
    }
}

/* -------------------------------------------------------------------------- */
/*  pie_blend_solid_row — solid color alpha blend                              */
/* -------------------------------------------------------------------------- */

/*
 * Scalar blend helper using the packed-RB trick.
 * Blends a single pixel in place.
 */
static inline void blend_pixel_scalar(uint32_t *px,
                                      uint32_t inv_alpha,
                                      uint32_t src_rb_a,
                                      uint32_t src_g_a)
{
    uint32_t d = *px;
    uint32_t d_rb = d & 0x00FF00FFU;
    uint32_t d_g  = (d >> 8) & 0xFFU;
    uint32_t out_rb = ((d_rb * inv_alpha + src_rb_a) >> 8) & 0x00FF00FFU;
    uint32_t out_g  = ((d_g  * inv_alpha + src_g_a)  >> 8) & 0xFFU;
    *px = out_rb | (out_g << 8) | 0xFF000000U;
}

void pie_blend_solid_row(uint32_t *dst, int count,
                         uint8_t src_r, uint8_t src_g, uint8_t src_b,
                         uint8_t alpha)
{
    /*
     * For each pixel: out[c] = (dst[c] * inv_alpha + src[c] * alpha) >> 8
     * Alpha channel is forced to 0xFF.
     *
     * PIE strategy using QACC (512-bit vector accumulator) to avoid
     * rounding differences vs the scalar code:
     *   1. Zero QACC
     *   2. Load 4 dst pixels into q0
     *   3. VMULAS q0, q1  →  QACC += dst[c] * inv_alpha (per-byte)
     *   4. VMULAS q3, q4  →  QACC += src[c] * alpha
     *   5. SRCMB.U8.QACC  →  extract (QACC >> 8) as uint8_t
     *   6. OR with alpha mask to force A=0xFF
     *   7. Store result
     *
     * Each byte gets its own 32-bit accumulator segment in QACC, so the
     * products are summed with full precision before the final shift,
     * matching the scalar (dst*inv_alpha + src*alpha) >> 8 exactly.
     */

    const uint8_t inv_alpha = 255 - alpha;

    /* Pre-compute scalar blend constants for head/tail pixels */
    const uint32_t src_rb_a = ((uint32_t)src_r * alpha) |
                              (((uint32_t)src_b * alpha) << 16);
    const uint32_t src_g_a  = (uint32_t)src_g * alpha;

    uint32_t *p = dst;
    int rem = count;

    /* Scalar head to reach 16-byte alignment */
    int head = align_head(p);
    if (head > rem) head = rem;
    for (int i = 0; i < head; i++) {
        blend_pixel_scalar(p++, inv_alpha, src_rb_a, src_g_a);
    }
    rem -= head;

    if (rem >= 4) {
        /* Build constant vectors for the PIE loop */
        uint8_t __attribute__((aligned(16))) inv_alpha_vec[16];
        uint8_t __attribute__((aligned(16))) src_vec[16];
        uint8_t __attribute__((aligned(16))) alpha_vec[16];
        uint8_t __attribute__((aligned(16))) alpha_mask[16];

        for (int i = 0; i < 4; i++) {
            /* inv_alpha for RGB channels, 0 for A channel */
            inv_alpha_vec[i * 4 + 0] = inv_alpha;
            inv_alpha_vec[i * 4 + 1] = inv_alpha;
            inv_alpha_vec[i * 4 + 2] = inv_alpha;
            inv_alpha_vec[i * 4 + 3] = 0;

            /* source color for RGB channels, 0 for A channel */
            src_vec[i * 4 + 0] = src_r;
            src_vec[i * 4 + 1] = src_g;
            src_vec[i * 4 + 2] = src_b;
            src_vec[i * 4 + 3] = 0;

            /* alpha for RGB channels, 0 for A channel */
            alpha_vec[i * 4 + 0] = alpha;
            alpha_vec[i * 4 + 1] = alpha;
            alpha_vec[i * 4 + 2] = alpha;
            alpha_vec[i * 4 + 3] = 0;

            /* mask to force output alpha = 0xFF */
            alpha_mask[i * 4 + 0] = 0;
            alpha_mask[i * 4 + 1] = 0;
            alpha_mask[i * 4 + 2] = 0;
            alpha_mask[i * 4 + 3] = 0xFF;
        }

        /* Load constant vectors */
        {
            uint8_t *p0 = inv_alpha_vec;
            uint8_t *p1 = src_vec;
            uint8_t *p2 = alpha_vec;
            uint8_t *p3 = alpha_mask;
            __asm__ volatile("esp.vld.128.ip q1, %0, 0" : "+r"(p0) : : "memory");
            __asm__ volatile("esp.vld.128.ip q3, %0, 0" : "+r"(p1) : : "memory");
            __asm__ volatile("esp.vld.128.ip q4, %0, 0" : "+r"(p2) : : "memory");
            __asm__ volatile("esp.vld.128.ip q5, %0, 0" : "+r"(p3) : : "memory");
        }

        /* Shift amount for SRCMB extraction */
        unsigned shift = 8;

        /* Process 4 pixels at a time (pointer is now 16-byte aligned) */
        int n4 = rem >> 2;
        for (int i = 0; i < n4; i++) {
            __asm__ volatile(
                "esp.zero.qacc                    \n"  /* QACC = 0 */
                "esp.vld.128.ip  q0, %0, 0        \n"  /* q0 = 4 dst pixels */
                "esp.vmulas.u8.qacc  q0, q1       \n"  /* QACC += dst * inv_alpha */
                "esp.vmulas.u8.qacc  q3, q4       \n"  /* QACC += src * alpha */
                "esp.srcmb.u8.qacc   q0, %1, 0    \n"  /* q0 = (QACC >> 8) as u8 */
                "esp.orq             q0, q0, q5   \n"  /* force alpha = 0xFF */
                "esp.vst.128.ip  q0, %0, 16       \n"  /* store & advance */
                : "+r"(p)
                : "r"(shift)
                : "memory"
            );
        }
    }

    /* Scalar tail */
    int tail = rem & 3;
    for (int i = 0; i < tail; i++) {
        blend_pixel_scalar(p + i, inv_alpha, src_rb_a, src_g_a);
    }
}

/* -------------------------------------------------------------------------- */
/*  pie_swap_rb — R↔B channel swap using PIE vector shifts                     */
/* -------------------------------------------------------------------------- */

void pie_swap_rb(uint32_t *buf, int count)
{
    /*
     * Swap bytes 0 and 2 within each 32-bit pixel:
     *   {R,G,B,A} (0xAABBGGRR) → {B,G,R,A} (0xAARRGGBB)
     *
     * Strategy using PIE 32-bit element shifts:
     *   1. q0 = original pixels
     *   2. q1 = q0 >> 16  → B moves to byte 0 (R position)
     *   3. q2 = q0 << 16  → R moves to byte 2 (B position)
     *   4. Mask and combine: (q0 & GA_mask) | (q1 & R_mask) | (q2 & B_mask)
     */

    uint32_t *p = buf;
    int rem = count;

    /* Scalar head for alignment */
    int head = align_head(p);
    if (head > rem) head = rem;
    for (int i = 0; i < head; i++) {
        uint32_t px = *p;
        uint32_t r = px & 0xFFU;
        uint32_t b = (px >> 16) & 0xFFU;
        *p++ = (px & 0xFF00FF00U) | (r << 16) | b;
    }
    rem -= head;

    if (rem >= 4) {
        /* Prepare mask vectors */
        uint32_t mask_ga = 0xFF00FF00U;
        uint32_t mask_lo = 0x000000FFU;
        uint32_t mask_hi = 0x00FF0000U;

        __asm__ volatile("esp.vldbc.32.ip q3, %0, 0" : : "r"(&mask_ga) : "memory");
        __asm__ volatile("esp.vldbc.32.ip q4, %0, 0" : : "r"(&mask_lo) : "memory");
        __asm__ volatile("esp.vldbc.32.ip q5, %0, 0" : : "r"(&mask_hi) : "memory");

        unsigned sar_val = 16;
        __asm__ volatile("esp.movx.w.sar %0" : : "r"(sar_val));

        int n4 = rem >> 2;
        for (int i = 0; i < n4; i++) {
            __asm__ volatile(
                "esp.vld.128.ip  q0, %0, 0  \n"
                "esp.vsr.u32     q1, q0      \n"
                "esp.vsl.32      q2, q0      \n"
                "esp.andq        q0, q0, q3  \n"
                "esp.andq        q1, q1, q4  \n"
                "esp.andq        q2, q2, q5  \n"
                "esp.orq         q0, q0, q1  \n"
                "esp.orq         q0, q0, q2  \n"
                "esp.vst.128.ip  q0, %0, 16 \n"
                : "+r"(p)
                :
                : "memory"
            );
        }
    }

    /* Scalar tail */
    int tail = rem & 3;
    for (int i = 0; i < tail; i++) {
        uint32_t px = p[i];
        uint32_t r = px & 0xFFU;
        uint32_t b = (px >> 16) & 0xFFU;
        p[i] = (px & 0xFF00FF00U) | (r << 16) | b;
    }
}
