// SPDX-FileCopyrightText: 2019 LAK132
// SPDX-License-Identifier: MIT

#ifndef SOFTRASTER_BARYCENTRIC_H
#define SOFTRASTER_BARYCENTRIC_H

#include "utils.h"

template<typename POS, typename COL>
struct bary_t
{
  point_t<POS> p0, p1;
  pixel_t<POS, COL> a, b, c;
  POS d00, d01, d11;
  float denom;
};

template<typename POS, typename COL>
inline bary_t<POS, COL> baryPre(const pixel_t<POS, COL> &a,
                                const pixel_t<POS, COL> &b,
                                const pixel_t<POS, COL> &c)
{
  bary_t<POS, COL> bary;
  bary.p0.x  = b.x - a.x;
  bary.p0.y  = b.y - a.y;
  bary.p1.x  = c.x - a.x;
  bary.p1.y  = c.y - a.y;
  bary.d00   = dot(bary.p0, bary.p0);
  bary.d01   = dot(bary.p0, bary.p1);
  bary.d11   = dot(bary.p1, bary.p1);
  bary.denom = 1.0f / ((bary.d00 * bary.d11) - (bary.d01 * bary.d01));
  bary.a     = a;
  bary.b     = b;
  bary.c     = c;
  return bary;
}

template<typename POS, typename COL>
inline void barycentricCol(pixel_t<POS, COL> &p, const bary_t<POS, COL> &bary)
{
  pixel_t<POS, COL> p2;
  p2.x    = p.x - bary.a.x;
  p2.y    = p.y - bary.a.y;
  POS d20 = dot(p2, bary.p0);
  POS d21 = dot(p2, bary.p1);
  float v = (bary.d11 * d20 - bary.d01 * d21) * bary.denom;
  float w = (bary.d00 * d21 - bary.d01 * d20) * bary.denom;
  float u = 1.0f - v - w;
  p.c     = (bary.a.c * u) + (bary.b.c * v) + (bary.c.c * w);
}

template<typename POS, typename COL>
inline void barycentricUV(pixel_t<POS, COL> &p, const bary_t<POS, COL> &bary)
{
  pixel_t<POS, COL> p2;
  p2.x    = p.x - bary.a.x;
  p2.y    = p.y - bary.a.y;
  POS d20 = dot(p2, bary.p0);
  POS d21 = dot(p2, bary.p1);
  float v = (bary.d11 * d20 - bary.d01 * d21) * bary.denom;
  float w = (bary.d00 * d21 - bary.d01 * d20) * bary.denom;
  float u = 1.0f - v - w;
  p.u     = (bary.a.u * u) + (bary.b.u * v) + (bary.c.u * w);
  p.v     = (bary.a.v * u) + (bary.b.v * v) + (bary.c.v * w);
}

template<typename POS, typename COL>
inline void barycentricUVCol(pixel_t<POS, COL> &p,
                             const bary_t<POS, COL> &bary)
{
  pixel_t<POS, COL> p2;
  p2.x    = p.x - bary.a.x;
  p2.y    = p.y - bary.a.y;
  POS d20 = dot(p2, bary.p0);
  POS d21 = dot(p2, bary.p1);
  float v = (bary.d11 * d20 - bary.d01 * d21) * bary.denom;
  float w = (bary.d00 * d21 - bary.d01 * d20) * bary.denom;
  float u = 1.0f - v - w;
  p.u     = (bary.a.u * u) + (bary.b.u * v) + (bary.c.u * w);
  p.v     = (bary.a.v * u) + (bary.b.v * v) + (bary.c.v * w);
  p.c     = (bary.a.c * u) + (bary.b.c * v) + (bary.c.c * w);
}

// Precomputed increments for scanline-based triangle rasterization.
// All three orient values and barycentric weights (v, w) are linear in
// screen x/y, so they can be stepped with additions instead of
// recomputing from scratch each pixel.
template<typename POS>
struct tri_increments_t
{
  // orient() increments per x-step and y-step for three edges
  POS orient_dx[3];
  POS orient_dy[3];
  // barycentric weight increments
  float v_dx, v_dy;
  float w_dx, w_dy;
};

template<typename POS, typename COL>
inline tri_increments_t<POS> triIncrementsPre(const bary_t<POS, COL> &bary)
{
  tri_increments_t<POS> inc;

  // Edge 0: {b, c} with other = a  — orient = -halfspace({b,c}, point) + bias
  // halfspace_dx = -(c.y - b.y),  orient_dx = (c.y - b.y)
  inc.orient_dx[0] = bary.c.y - bary.b.y;
  inc.orient_dy[0] = -(bary.c.x - bary.b.x);

  // Edge 1: {c, a} with other = b
  inc.orient_dx[1] = bary.a.y - bary.c.y;
  inc.orient_dy[1] = -(bary.a.x - bary.c.x);

  // Edge 2: {a, b} with other = c
  inc.orient_dx[2] = bary.b.y - bary.a.y;
  inc.orient_dy[2] = -(bary.b.x - bary.a.x);

  // Barycentric weight increments:
  // d20 += bary.p0.x per x-step, d21 += bary.p1.x per x-step
  // v = (d11*d20 - d01*d21) * denom  =>  v_dx = (d11*p0.x - d01*p1.x) * denom
  inc.v_dx = (bary.d11 * bary.p0.x - bary.d01 * bary.p1.x) * bary.denom;
  inc.v_dy = (bary.d11 * bary.p0.y - bary.d01 * bary.p1.y) * bary.denom;
  inc.w_dx = (bary.d00 * bary.p1.x - bary.d01 * bary.p0.x) * bary.denom;
  inc.w_dy = (bary.d00 * bary.p1.y - bary.d01 * bary.p0.y) * bary.denom;

  return inc;
}

#endif
