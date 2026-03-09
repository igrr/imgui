// SPDX-FileCopyrightText: 2019 LAK132
// SPDX-License-Identifier: MIT

#ifndef SOFTRASTER_H
#define SOFTRASTER_H

#include "barycentric.h"
#include "color.h"
#include "defines.h"
#include "texture.h"
#include "utils.h"

/*
 * Optional profiling hooks.  Define these macros before including softraster.h
 * to instrument the renderCommand() template without duplicating its code.
 *
 *   SOFTRASTER_BEFORE_QUAD() / SOFTRASTER_AFTER_QUAD()
 *   SOFTRASTER_BEFORE_TRI()  / SOFTRASTER_AFTER_TRI()
 */
#ifndef SOFTRASTER_BEFORE_QUAD
#define SOFTRASTER_BEFORE_QUAD()  /* empty */
#endif
#ifndef SOFTRASTER_AFTER_QUAD
#define SOFTRASTER_AFTER_QUAD()   /* empty */
#endif
#ifndef SOFTRASTER_BEFORE_TRI
#define SOFTRASTER_BEFORE_TRI()   /* empty */
#endif
#ifndef SOFTRASTER_AFTER_TRI
#define SOFTRASTER_AFTER_TRI()    /* empty */
#endif

/* Pixel-level profiling hooks (default: no-op) */
#ifndef SOFTRASTER_QUAD_PIXELS
#define SOFTRASTER_QUAD_PIXELS(n)       /* empty */
#endif
#ifndef SOFTRASTER_TRI_PIXEL_HIT
#define SOFTRASTER_TRI_PIXEL_HIT()      /* empty */
#endif
#ifndef SOFTRASTER_TRI_BBOX_PIXELS
#define SOFTRASTER_TRI_BBOX_PIXELS(n)   /* empty */
#endif
#ifndef SOFTRASTER_QUAD_CATEGORY
#define SOFTRASTER_QUAD_CATEGORY(is_solid, is_blit, px) /* empty */
#endif

template<bool alphaBlend, typename POS, typename SCREEN, typename TEXTURE, typename COLOR>
void renderQuadCore(texture_t<SCREEN> &screen,
                    const texture_t<TEXTURE> &tex,
                    const clip_t<POS> &clip,
                    const rectangle_t<POS, COLOR> &quad)
{
  if ((quad.p2.x < clip.x.min) || (quad.p2.y < clip.y.min) ||
      (quad.p1.x >= clip.x.max) || (quad.p1.y >= clip.y.max))
    return;

  const range_t<POS> qx = {quad.p1.x, quad.p2.x};
  const range_t<POS> qy = {quad.p1.y, quad.p2.y};

  const range_t<float> qu = {inl_max((float)quad.p1.u * tex.w, 0.0f),
                             inl_min((float)quad.p2.u * tex.w, (float)tex.w)};

  const range_t<float> qv = {inl_max((float)quad.p1.v * tex.h, 0.0f),
                             inl_min((float)quad.p2.v * tex.h, (float)tex.h)};

  const range_t<POS> rx = inl_min(qx, clip.x);
  const range_t<POS> ry = inl_min(qy, clip.y);

  const float duDx = (qu.max - qu.min) / (qx.max - qx.min);
  const float dvDy = (qv.max - qv.min) / (qy.max - qy.min);

  const float xoffset = (float)rx.min - (float)qx.min;
  const float yoffset = (float)ry.min - (float)qy.min;

  const float startu = qu.min + (xoffset > 0 ? duDx * xoffset : 0);
  const float startv = qv.min + (yoffset > 0 ? dvDy * yoffset : 0);

  bool blit = ((duDx == 1.0f) && (dvDy == 1.0f));

  {
    int64_t _qpx = (int64_t)(rx.max - rx.min) * (ry.max - ry.min);
    SOFTRASTER_QUAD_PIXELS(_qpx);
    SOFTRASTER_QUAD_CATEGORY(false, blit, _qpx);
  }

  if (blit)
  {
    const POS u = static_cast<POS>(startu - rx.min);
    const POS v = static_cast<POS>(startv - ry.min);
    if (alphaBlend)
    {
      for (POS y = ry.min; y < ry.max; ++y)
      {
        for (POS x = rx.min; x < rx.max; ++x)
        {
          screen.at_unchecked(static_cast<size_t>(x), static_cast<size_t>(y)) %=
            quad.p1.c *
            tex.at_unchecked(static_cast<size_t>(x + u), static_cast<size_t>(y + v));
        }
      }
    }
    else
    {
      for (POS y = ry.min; y < ry.max; ++y)
      {
        for (POS x = rx.min; x < rx.max; ++x)
        {
          screen.at_unchecked(static_cast<size_t>(x), static_cast<size_t>(y)) =
            quad.p1.c *
            tex.at_unchecked(static_cast<size_t>(x + u), static_cast<size_t>(y + v));
        }
      }
    }
  }
  else
  {
    if (alphaBlend)
    {
      float v = startv;
      POS y   = ry.min;
      while (y < ry.max)
      {
        float u = startu;
        POS x   = rx.min;
        while (x < rx.max)
        {
          screen.at_unchecked(static_cast<size_t>(x), static_cast<size_t>(y)) %=
            quad.p1.c * tex.at_unchecked(static_cast<size_t>(u), static_cast<size_t>(v));
          ++x;
          u += duDx;
        }
        ++y;
        v += dvDy;
      }
    }
    else
    {
      float v = startv;
      POS y   = ry.min;
      while (y < ry.max)
      {
        float u = startu;
        POS x   = rx.min;
        while (x < rx.max)
        {
          screen.at_unchecked(static_cast<size_t>(x), static_cast<size_t>(y)) =
            quad.p1.c * tex.at_unchecked(static_cast<size_t>(u), static_cast<size_t>(v));
          ++x;
          u += duDx;
        }
        ++y;
        v += dvDy;
      }
    }
  }
}

template<bool alphaBlend, typename POS, typename SCREEN, typename COLOR>
void renderQuadCore(texture_t<SCREEN> &screen,
                    const clip_t<POS> &clip,
                    const rectangle_t<POS, COLOR> &quad)
{
  if ((quad.p2.x < clip.x.min) || (quad.p2.y < clip.y.min) ||
      (quad.p1.x >= clip.x.max) || (quad.p1.y >= clip.y.max))
    return;

  const range_t<POS> rx = inl_min({quad.p1.x, quad.p2.x}, clip.x);
  const range_t<POS> ry = inl_min({quad.p1.y, quad.p2.y}, clip.y);

  {
    int64_t _qpx = (int64_t)(rx.max - rx.min) * (ry.max - ry.min);
    SOFTRASTER_QUAD_PIXELS(_qpx);
    SOFTRASTER_QUAD_CATEGORY(true, false, _qpx);
  }

  if (alphaBlend)
  {
    for (POS y = ry.min; y < ry.max; ++y)
    {
      for (POS x = rx.min; x < rx.max; ++x)
      {
        screen.at_unchecked(static_cast<size_t>(x), static_cast<size_t>(y)) %= quad.p1.c;
      }
    }
  }
  else
  {
    for (POS y = ry.min; y < ry.max; ++y)
    {
      for (POS x = rx.min; x < rx.max; ++x)
      {
        screen.at_unchecked(static_cast<size_t>(x), static_cast<size_t>(y)) = quad.p1.c;
      }
    }
  }
}

template<bool alphaBlend, typename POS, typename SCREEN, typename COLOR>
void renderQuad(texture_t<SCREEN> &screen,
                const texture_base_t *tex,
                const clip_t<POS> &clip,
                const rectangle_t<POS, COLOR> &quad)
{
  switch (tex == nullptr ? texture_type_t::NONE : tex->type)
  {
    case texture_type_t::ALPHA8:
      renderQuadCore<alphaBlend>(screen,
                     *reinterpret_cast<const texture_alpha8_t *>(tex),
                     clip,
                     quad);
      break;

    case texture_type_t::VALUE8:
      renderQuadCore<alphaBlend>(screen,
                     *reinterpret_cast<const texture_value8_t *>(tex),
                     clip,
                     quad);
      break;

    case texture_type_t::COLOR16:
      renderQuadCore<alphaBlend>(screen,
                     *reinterpret_cast<const texture_color16_t *>(tex),
                     clip,
                     quad);
      break;

    case texture_type_t::COLOR24:
      renderQuadCore<alphaBlend>(screen,
                     *reinterpret_cast<const texture_color24_t *>(tex),
                     clip,
                     quad);
      break;

    case texture_type_t::COLOR32:
      renderQuadCore<alphaBlend>(screen,
                     *reinterpret_cast<const texture_color32_t *>(tex),
                     clip,
                     quad);
      break;

    default: renderQuadCore<alphaBlend>(screen, clip, quad); break;
  }
}

template<typename POS, typename COLOR, typename TEXTURE>
point_t<POS> texCoord(const pixel_t<POS, COLOR> &p,
                      const texture_t<TEXTURE> &tex)
{
  POS u = (POS)((p.u * tex.w) + 0.5f);
  POS v = (POS)((p.v * tex.h) + 0.5f);
  // Clamp to valid texture range (avoids expensive fmodf/integer divide).
  // ImGui UVs are always in [0,1] for the font atlas, so this is a no-op
  // in the common case; clamp handles edge rounding gracefully.
  if (u < 0) u = 0;
  else if (u >= (POS)tex.w) u = (POS)tex.w - 1;
  if (v < 0) v = 0;
  else if (v >= (POS)tex.h) v = (POS)tex.h - 1;
  return {u, v};
}

template<bool alphaBlend, typename POS, typename SCREEN, typename TEXTURE, typename COLOR>
void renderTriCore(texture_t<SCREEN> &screen,
                   const texture_t<TEXTURE> &tex,
                   const clip_t<POS> &clip,
                   const range_t<POS> &rY,
                   const range_t<POS> &rX,
                   const bary_t<POS, COLOR> &bary)
{
  const range_t<size_t> ry = {(size_t)inl_max(rY.min, clip.y.min),
                              (size_t)inl_min(rY.max, clip.y.max)};
  const range_t<size_t> rx = {(size_t)inl_max(rX.min, clip.x.min),
                              (size_t)inl_min(rX.max, clip.x.max)};
  SOFTRASTER_TRI_BBOX_PIXELS((int64_t)(ry.max - ry.min) * (rx.max - rx.min));

  const tri_increments_t<POS> inc = triIncrementsPre(bary);
  const point_t<POS> startPt = {(POS)rx.min, (POS)ry.min};

  // Compute initial orient values at (rx.min, ry.min)
  POS orient_row[3] = {
    orient(edge_t<POS>{point_t<POS>{bary.b.x, bary.b.y}, point_t<POS>{bary.c.x, bary.c.y}},
           point_t<POS>{bary.a.x, bary.a.y}, startPt),
    orient(edge_t<POS>{point_t<POS>{bary.c.x, bary.c.y}, point_t<POS>{bary.a.x, bary.a.y}},
           point_t<POS>{bary.b.x, bary.b.y}, startPt),
    orient(edge_t<POS>{point_t<POS>{bary.a.x, bary.a.y}, point_t<POS>{bary.b.x, bary.b.y}},
           point_t<POS>{bary.c.x, bary.c.y}, startPt)
  };

  // Compute initial barycentric weights at (rx.min, ry.min)
  POS p2x_init = startPt.x - bary.a.x;
  POS p2y_init = startPt.y - bary.a.y;
  POS d20_init = p2x_init * bary.p0.x + p2y_init * bary.p0.y;
  POS d21_init = p2x_init * bary.p1.x + p2y_init * bary.p1.y;
  float v_row = (bary.d11 * d20_init - bary.d01 * d21_init) * bary.denom;
  float w_row = (bary.d00 * d21_init - bary.d01 * d20_init) * bary.denom;

  for (size_t y = ry.min; y < ry.max; ++y)
  {
    POS o0 = orient_row[0], o1 = orient_row[1], o2 = orient_row[2];
    float v = v_row, w = w_row;

    for (size_t x = rx.min; x < rx.max; ++x)
    {
      if (o0 >= 0 && o1 >= 0 && o2 >= 0)
      {
        SOFTRASTER_TRI_PIXEL_HIT();
        float u_bary = 1.0f - v - w;
        pixel_t<POS, COLOR> p;
        p.u = (bary.a.u * u_bary) + (bary.b.u * v) + (bary.c.u * w);
        p.v = (bary.a.v * u_bary) + (bary.b.v * v) + (bary.c.v * w);

        point_t<POS> coord = texCoord(p, tex);

        if (alphaBlend)
          screen.at_unchecked(x, y) %=
            bary.a.c *
            tex.at_unchecked(static_cast<size_t>(coord.x), static_cast<size_t>(coord.y));
        else
          screen.at_unchecked(x, y) =
            bary.a.c *
            tex.at_unchecked(static_cast<size_t>(coord.x), static_cast<size_t>(coord.y));
      }
      o0 += inc.orient_dx[0];
      o1 += inc.orient_dx[1];
      o2 += inc.orient_dx[2];
      v += inc.v_dx;
      w += inc.w_dx;
    }
    orient_row[0] += inc.orient_dy[0];
    orient_row[1] += inc.orient_dy[1];
    orient_row[2] += inc.orient_dy[2];
    v_row += inc.v_dy;
    w_row += inc.w_dy;
  }
}

template<bool alphaBlend, typename POS, typename SCREEN, typename COLOR>
void renderTriCore(texture_t<SCREEN> &screen,
                   const clip_t<POS> &clip,
                   const range_t<POS> &rY,
                   const range_t<POS> &rX,
                   const bary_t<POS, COLOR> &bary,
                   const bool uvBlend)
{
  const range_t<size_t> ry = {(size_t)inl_max(rY.min, clip.y.min),
                              (size_t)inl_min(rY.max, clip.y.max)};
  const range_t<size_t> rx = {(size_t)inl_max(rX.min, clip.x.min),
                              (size_t)inl_min(rX.max, clip.x.max)};

  SOFTRASTER_TRI_BBOX_PIXELS((int64_t)(ry.max - ry.min) * (rx.max - rx.min));

  const tri_increments_t<POS> inc = triIncrementsPre(bary);
  const point_t<POS> startPt = {(POS)rx.min, (POS)ry.min};

  POS orient_row[3] = {
    orient(edge_t<POS>{point_t<POS>{bary.b.x, bary.b.y}, point_t<POS>{bary.c.x, bary.c.y}},
           point_t<POS>{bary.a.x, bary.a.y}, startPt),
    orient(edge_t<POS>{point_t<POS>{bary.c.x, bary.c.y}, point_t<POS>{bary.a.x, bary.a.y}},
           point_t<POS>{bary.b.x, bary.b.y}, startPt),
    orient(edge_t<POS>{point_t<POS>{bary.a.x, bary.a.y}, point_t<POS>{bary.b.x, bary.b.y}},
           point_t<POS>{bary.c.x, bary.c.y}, startPt)
  };

  if (uvBlend)
  {
    POS p2x_init = startPt.x - bary.a.x;
    POS p2y_init = startPt.y - bary.a.y;
    POS d20_init = p2x_init * bary.p0.x + p2y_init * bary.p0.y;
    POS d21_init = p2x_init * bary.p1.x + p2y_init * bary.p1.y;
    float v_row = (bary.d11 * d20_init - bary.d01 * d21_init) * bary.denom;
    float w_row = (bary.d00 * d21_init - bary.d01 * d20_init) * bary.denom;

    for (size_t y = ry.min; y < ry.max; ++y)
    {
      POS o0 = orient_row[0], o1 = orient_row[1], o2 = orient_row[2];
      float v = v_row, w = w_row;

      for (size_t x = rx.min; x < rx.max; ++x)
      {
        if (o0 >= 0 && o1 >= 0 && o2 >= 0)
        {
          SOFTRASTER_TRI_PIXEL_HIT();
          float u_bary = 1.0f - v - w;
          COLOR c = (bary.a.c * u_bary) + (bary.b.c * v) + (bary.c.c * w);
          if (alphaBlend)
            screen.at_unchecked(x, y) %= c;
          else
            screen.at_unchecked(x, y) = c;
        }
        o0 += inc.orient_dx[0];
        o1 += inc.orient_dx[1];
        o2 += inc.orient_dx[2];
        v += inc.v_dx;
        w += inc.w_dx;
      }
      orient_row[0] += inc.orient_dy[0];
      orient_row[1] += inc.orient_dy[1];
      orient_row[2] += inc.orient_dy[2];
      v_row += inc.v_dy;
      w_row += inc.w_dy;
    }
  }
  else
  {
    for (size_t y = ry.min; y < ry.max; ++y)
    {
      POS o0 = orient_row[0], o1 = orient_row[1], o2 = orient_row[2];

      for (size_t x = rx.min; x < rx.max; ++x)
      {
        if (o0 >= 0 && o1 >= 0 && o2 >= 0)
        {
          SOFTRASTER_TRI_PIXEL_HIT();
          if (alphaBlend)
            screen.at_unchecked(x, y) %= bary.a.c;
          else
            screen.at_unchecked(x, y) = bary.a.c;
        }
        o0 += inc.orient_dx[0];
        o1 += inc.orient_dx[1];
        o2 += inc.orient_dx[2];
      }
      orient_row[0] += inc.orient_dy[0];
      orient_row[1] += inc.orient_dy[1];
      orient_row[2] += inc.orient_dy[2];
    }
  }
}

template<bool alphaBlend, typename POS, typename SCREEN, typename COLOR>
void renderTri(texture_t<SCREEN> &screen,
               const texture_base_t *tex,
               const clip_t<POS> &clip,
               const range_t<POS> &rY,
               const range_t<POS> &rX,
               const bary_t<POS, COLOR> &bary,
               const bool uvBlend)
{
  switch (tex == nullptr ? texture_type_t::NONE : tex->type)
  {
    case texture_type_t::ALPHA8:
      renderTriCore<alphaBlend>(screen,
                    *reinterpret_cast<const texture_alpha8_t *>(tex),
                    clip,
                    rY,
                    rX,
                    bary);
      break;

    case texture_type_t::VALUE8:
      renderTriCore<alphaBlend>(screen,
                    *reinterpret_cast<const texture_value8_t *>(tex),
                    clip,
                    rY,
                    rX,
                    bary);
      break;

    case texture_type_t::COLOR16:
      renderTriCore<alphaBlend>(screen,
                    *reinterpret_cast<const texture_color16_t *>(tex),
                    clip,
                    rY,
                    rX,
                    bary);
      break;

    case texture_type_t::COLOR24:
      renderTriCore<alphaBlend>(screen,
                    *reinterpret_cast<const texture_color24_t *>(tex),
                    clip,
                    rY,
                    rX,
                    bary);
      break;

    case texture_type_t::COLOR32:
      renderTriCore<alphaBlend>(screen,
                    *reinterpret_cast<const texture_color32_t *>(tex),
                    clip,
                    rY,
                    rX,
                    bary);
      break;

    default:
      renderTriCore<alphaBlend>(screen, clip, rY, rX, bary, uvBlend);
      break;
  }
}

template<bool alphaBlend, typename POS, typename SCREEN, typename COLOR>
void renderTri(texture_t<SCREEN> &screen,
               const texture_base_t *tex,
               const clip_t<POS> &clip,
               triangle_t<POS, COLOR> &tri,
               const bool uvBlend)
{
  renderTri<alphaBlend>(screen,
            tex,
            clip,
            {inl_min(inl_min(tri.p1.y, tri.p2.y), tri.p3.y),
             inl_max(inl_max(tri.p1.y, tri.p2.y), tri.p3.y) + 1},
            {inl_min(inl_min(tri.p1.x, tri.p2.x), tri.p3.x),
             inl_max(inl_max(tri.p1.x, tri.p2.x), tri.p3.x) + 1},
            baryPre(tri.p1, tri.p2, tri.p3),
            uvBlend);
}

template<typename POS, typename SCREEN>
void renderCommand(texture_t<SCREEN> &screen,
                   const texture_base_t *texture,
                   const ImDrawVert *vtx_buffer,
                   const ImDrawIdx *idx_buffer,
                   const ImDrawCmd &pcmd)
{
  const clip_t<POS> clip = {{inl_max((POS)pcmd.ClipRect.x, (POS)0),
                             inl_min((POS)pcmd.ClipRect.z, (POS)screen.w)},
                            {inl_max((POS)pcmd.ClipRect.y, (POS)0),
                             inl_min((POS)pcmd.ClipRect.w, (POS)screen.h)}};

  for (unsigned int i = 0; i < pcmd.ElemCount; i += 3)
  {
    const ImDrawVert *verts[] = {&vtx_buffer[idx_buffer[i]],
                                 &vtx_buffer[idx_buffer[i + 1]],
                                 &vtx_buffer[idx_buffer[i + 2]]};

    if (i < pcmd.ElemCount - 3)
    {
      ImVec2 tlpos = verts[0]->pos;
      ImVec2 brpos = verts[0]->pos;
      ImVec2 tluv  = verts[0]->uv;
      ImVec2 bruv  = verts[0]->uv;
      for (int v = 1; v < 3; v++)
      {
        if (verts[v]->pos.x < tlpos.x)
        {
          tlpos.x = verts[v]->pos.x;
          tluv.x  = verts[v]->uv.x;
        }
        else if (verts[v]->pos.x > brpos.x)
        {
          brpos.x = verts[v]->pos.x;
          bruv.x  = verts[v]->uv.x;
        }
        if (verts[v]->pos.y < tlpos.y)
        {
          tlpos.y = verts[v]->pos.y;
          tluv.y  = verts[v]->uv.y;
        }
        else if (verts[v]->pos.y > brpos.y)
        {
          brpos.y = verts[v]->pos.y;
          bruv.y  = verts[v]->uv.y;
        }
      }

      const ImDrawVert *nextVerts[] = {&vtx_buffer[idx_buffer[i + 3]],
                                       &vtx_buffer[idx_buffer[i + 4]],
                                       &vtx_buffer[idx_buffer[i + 5]]};

      bool isRect = true;
      for (int v = 0; v < 3; v++)
      {
        if (((nextVerts[v]->pos.x != tlpos.x) &&
             (nextVerts[v]->pos.x != brpos.x)) ||
            ((nextVerts[v]->pos.y != tlpos.y) &&
             (nextVerts[v]->pos.y != brpos.y)) ||
            ((nextVerts[v]->uv.x != tluv.x) &&
             (nextVerts[v]->uv.x != bruv.x)) ||
            ((nextVerts[v]->uv.y != tluv.y) && (nextVerts[v]->uv.y != bruv.y)))
        {
          isRect = false;
          break;
        }
      }

      if (isRect)
      {
        rectangle_t<POS, SCREEN> quad;
        quad.p1.x = static_cast<POS>(tlpos.x);
        quad.p1.y = static_cast<POS>(tlpos.y);
        quad.p2.x = static_cast<POS>(brpos.x);
        quad.p2.y = static_cast<POS>(brpos.y);
        quad.p1.u = tluv.x;
        quad.p1.v = tluv.y;
        quad.p2.u = bruv.x;
        quad.p2.v = bruv.y;
        quad.p1.c =
          color32_t(static_cast<uint8_t>(verts[0]->col >> IM_COL32_R_SHIFT),
                    static_cast<uint8_t>(verts[0]->col >> IM_COL32_G_SHIFT),
                    static_cast<uint8_t>(verts[0]->col >> IM_COL32_B_SHIFT),
                    static_cast<uint8_t>(verts[0]->col >> IM_COL32_A_SHIFT));
        quad.p2.c = quad.p1.c;

        const bool noUV = (quad.p1.u == quad.p2.u) && (quad.p1.v == quad.p2.v);

        SOFTRASTER_BEFORE_QUAD();
        renderQuad<true>(screen, noUV ? nullptr : texture, clip, quad);
        SOFTRASTER_AFTER_QUAD();

        i += 3;
        continue;
      }
    }

    triangle_t<POS, SCREEN> tri;
    // triangle_t<POS, color32_t> tri;
    tri.p1.x = static_cast<POS>(verts[0]->pos.x);
    tri.p1.y = static_cast<POS>(verts[0]->pos.y);
    tri.p1.u = verts[0]->uv.x;
    tri.p1.v = verts[0]->uv.y;
    tri.p1.c =
      color32_t(static_cast<uint8_t>(verts[0]->col >> IM_COL32_R_SHIFT),
                static_cast<uint8_t>(verts[0]->col >> IM_COL32_G_SHIFT),
                static_cast<uint8_t>(verts[0]->col >> IM_COL32_B_SHIFT),
                static_cast<uint8_t>(verts[0]->col >> IM_COL32_A_SHIFT));

    tri.p2.x = static_cast<POS>(verts[1]->pos.x);
    tri.p2.y = static_cast<POS>(verts[1]->pos.y);
    tri.p2.u = verts[1]->uv.x;
    tri.p2.v = verts[1]->uv.y;
    tri.p2.c =
      color32_t(static_cast<uint8_t>(verts[1]->col >> IM_COL32_R_SHIFT),
                static_cast<uint8_t>(verts[1]->col >> IM_COL32_G_SHIFT),
                static_cast<uint8_t>(verts[1]->col >> IM_COL32_B_SHIFT),
                static_cast<uint8_t>(verts[1]->col >> IM_COL32_A_SHIFT));

    tri.p3.x = static_cast<POS>(verts[2]->pos.x);
    tri.p3.y = static_cast<POS>(verts[2]->pos.y);
    tri.p3.u = verts[2]->uv.x;
    tri.p3.v = verts[2]->uv.y;
    tri.p3.c =
      color32_t(static_cast<uint8_t>(verts[2]->col >> IM_COL32_R_SHIFT),
                static_cast<uint8_t>(verts[2]->col >> IM_COL32_G_SHIFT),
                static_cast<uint8_t>(verts[2]->col >> IM_COL32_B_SHIFT),
                static_cast<uint8_t>(verts[2]->col >> IM_COL32_A_SHIFT));

    // Make sure the winding order is correct.
    if (halfspace(edge_t<POS>{point_t<POS>{tri.p1.x, tri.p1.y},
                              point_t<POS>{tri.p2.x, tri.p2.y}},
                  point_t<POS>{tri.p3.x, tri.p3.y}) > 0)
      swap(&tri.p2, &tri.p3);

    const bool noUV = (tri.p1.u == tri.p2.u) && (tri.p1.u == tri.p3.u) &&
                      (tri.p1.v == tri.p2.v) && (tri.p1.v == tri.p3.v);
    const bool flatCol =
      noUV || ((tri.p1.c == tri.p2.c) && (tri.p1.c == tri.p3.c));

    SOFTRASTER_BEFORE_TRI();
    renderTri<true>(
      screen, noUV ? nullptr : texture, clip, tri, !flatCol);
    SOFTRASTER_AFTER_TRI();
  }
}

template<typename POS, typename SCREEN>
void renderDrawLists(ImDrawData *drawData, texture_t<SCREEN> &screen)
{
  ImGuiIO &io  = ImGui::GetIO();
  int fbWidth  = (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x);
  int fbHeight = (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y);
  if (fbWidth == 0 || fbHeight == 0) return;
  drawData->ScaleClipRects(io.DisplayFramebufferScale);

  for (int n = 0; n < drawData->CmdListsCount; n++)
  {
    const ImDrawList *cmdList    = drawData->CmdLists[n];
    const ImDrawVert *vtx_buffer = cmdList->VtxBuffer.Data;
    const ImDrawIdx *idx_buffer  = cmdList->IdxBuffer.Data;

    for (int cmdi = 0; cmdi < cmdList->CmdBuffer.Size; cmdi++)
    {
      const ImDrawCmd &pcmd = cmdList->CmdBuffer[cmdi];
      if (pcmd.UserCallback)
      {
        pcmd.UserCallback(cmdList, &pcmd);
      }
      else
      {
        renderCommand<POS>(
          screen,
          reinterpret_cast<const texture_base_t *>(pcmd.GetTexID()),
          vtx_buffer,
          idx_buffer,
          pcmd);
      }
      idx_buffer += pcmd.ElemCount;
    }
  }
}

#endif
