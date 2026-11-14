// cursor-finder-core — the marker as a picture (spike 004-21). See pointer_picture.h.
#include "core/pointer_picture.h"

#include <cmath>
#include <initializer_list>

#include "core/marker.h"

namespace cursor_finder {

int layer_alpha(int opacity_pct)
{
    return 255 * clamp_int(opacity_pct, 0, 100) / 100;
}

int outline_alpha(int opacity_pct)
{
    return 255 * clamp_int(opacity_pct, 0, 100) / 100 * 9 / 10;
}

int fill_alpha(int fill_opacity_pct, int opacity_pct)
{
    return 255 * clamp_int(fill_opacity_pct, 0, 100) / 100 * clamp_int(opacity_pct, 0, 100) /
           100;
}

namespace {

// Sample offsets inside a pixel: a 4 x 4 grid.
constexpr int kSamples = 4;

float sample_offset(int k) { return (static_cast<float>(k) + 0.5f) / kSamples; }

struct Bounds {
    int x0, y0, x1, y1; // pixels [x0, x1) x [y0, y1), clipped to the canvas
};

Bounds clip_bounds(const PremulCanvas& c, float min_x, float min_y, float max_x, float max_y)
{
    Bounds b{static_cast<int>(std::floor(min_x)), static_cast<int>(std::floor(min_y)),
             static_cast<int>(std::ceil(max_x)) + 1, static_cast<int>(std::ceil(max_y)) + 1};
    if (b.x0 < 0) { b.x0 = 0; }
    if (b.y0 < 0) { b.y0 = 0; }
    if (b.x1 > c.width) { b.x1 = c.width; }
    if (b.y1 > c.height) { b.y1 = c.height; }
    return b;
}

// Blend `colour` at `alpha` (0-255) times the coverage over pixel (x, y).
void blend(PremulCanvas& c, int x, int y, const Rgb& colour, int alpha, float coverage)
{
    if (coverage <= 0.0f) { return; }
    const float a = static_cast<float>(alpha) / 255.0f * coverage;
    float* d = &c.px[(static_cast<std::size_t>(y) * c.width + x) * 4];
    const float keep = 1.0f - a;
    d[0] = static_cast<float>(colour.r) / 255.0f * a + d[0] * keep;
    d[1] = static_cast<float>(colour.g) / 255.0f * a + d[1] * keep;
    d[2] = static_cast<float>(colour.b) / 255.0f * a + d[2] * keep;
    d[3] = a + d[3] * keep;
}

// A filled disc, blended as one shape.
void paint_disc(PremulCanvas& c, float cx, float cy, float r, const Rgb& colour, int alpha)
{
    if (r <= 0.0f || alpha <= 0) { return; }
    const Bounds b = clip_bounds(c, cx - r, cy - r, cx + r, cy + r);
    const float r2 = r * r;
    for (int y = b.y0; y < b.y1; ++y)
    {
        for (int x = b.x0; x < b.x1; ++x)
        {
            int hits = 0;
            for (int sy = 0; sy < kSamples; ++sy)
            {
                const float dy = static_cast<float>(y) + sample_offset(sy) - cy;
                for (int sx = 0; sx < kSamples; ++sx)
                {
                    const float dx = static_cast<float>(x) + sample_offset(sx) - cx;
                    if (dx * dx + dy * dy <= r2) { ++hits; }
                }
            }
            blend(c, x, y, colour, alpha, static_cast<float>(hits) / (kSamples * kSamples));
        }
    }
}

float edge(const MarkerPoint& a, const MarkerPoint& b, float px, float py)
{
    return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
}

bool in_triangle(const MarkerPoint& a, const MarkerPoint& b, const MarkerPoint& c, float px,
                 float py)
{
    const float e0 = edge(a, b, px, py);
    const float e1 = edge(b, c, px, py);
    const float e2 = edge(c, a, px, py);
    return (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) || (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f);
}

// One dash's mesh, blended as one shape (a sample counts once however many of the
// mesh's triangles hold it).
void paint_dash(PremulCanvas& c, const DashBoundary& d, const Rgb& colour, int alpha)
{
    if (d.count < 3 || alpha <= 0) { return; }
    float min_x = d.points[0].x, max_x = min_x, min_y = d.points[0].y, max_y = min_y;
    for (int i = 1; i < d.count; ++i)
    {
        if (d.points[i].x < min_x) { min_x = d.points[i].x; }
        if (d.points[i].x > max_x) { max_x = d.points[i].x; }
        if (d.points[i].y < min_y) { min_y = d.points[i].y; }
        if (d.points[i].y > max_y) { max_y = d.points[i].y; }
    }
    const Bounds b = clip_bounds(c, min_x, min_y, max_x, max_y);
    for (int y = b.y0; y < b.y1; ++y)
    {
        for (int x = b.x0; x < b.x1; ++x)
        {
            int hits = 0;
            for (int sy = 0; sy < kSamples; ++sy)
            {
                const float py = static_cast<float>(y) + sample_offset(sy);
                for (int sx = 0; sx < kSamples; ++sx)
                {
                    const float px = static_cast<float>(x) + sample_offset(sx);
                    for (int t = 0; t < d.triangle_count; ++t)
                    {
                        const int* tri = d.triangles[t];
                        if (in_triangle(d.points[tri[0]], d.points[tri[1]], d.points[tri[2]],
                                        px, py))
                        {
                            ++hits;
                            break;
                        }
                    }
                }
            }
            blend(c, x, y, colour, alpha, static_cast<float>(hits) / (kSamples * kSamples));
        }
    }
}

std::uint8_t to_byte(float v)
{
    const float s = v * 255.0f + 0.5f;
    if (s <= 0.0f) { return 0; }
    if (s >= 255.0f) { return 255; }
    return static_cast<std::uint8_t>(s);
}

} // namespace

PremulCanvas paint_radar_dash(const CursorSettings& s, int side)
{
    PremulCanvas c;
    if (side <= 0) { return c; }
    c.width = side;
    c.height = side;
    c.px.assign(static_cast<std::size_t>(side) * side * 4, 0.0f);

    const float size = static_cast<float>(s.size_px);
    const float centre = static_cast<float>(side / 2);
    if (size <= 0.0f) { return c; }

    // 1. The fill disc (DrawMarker: FillTint, size x 0.35 x fill size).
    if (s.fill)
    {
        const float frac =
            static_cast<float>(clamp_int(s.fill_size_pct, kFillSizeMin, kFillSizeMax)) / 100.0f;
        paint_disc(c, centre, centre, size * kRadarDashFillExtentMax * frac, s.fill_colour,
                   fill_alpha(s.fill_opacity_pct, s.opacity_pct));
    }

    // 2-3. DrawRadarDash: nothing (outline included) when the colour is invisible.
    const RadarDashGeometry g =
        radar_dash_geometry(centre, centre, size, s.dash_count, s.dash_width_pct);
    const int colour_a = layer_alpha(s.opacity_pct);
    if (g.stroke <= 0.0f || colour_a == 0) { return c; }
    if (s.outline)
    {
        const int outline_a = outline_alpha(s.opacity_pct);
        for (int i = 0; i < g.dash_count; ++i)
        {
            paint_dash(c, dash_stroke_boundary(g.center, g.radius, g.outline_stroke, g.dashes[i]),
                       s.outline_colour, outline_a);
        }
        paint_disc(c, g.center.x, g.center.y, g.outline_dot_radius, s.outline_colour, outline_a);
    }
    for (int i = 0; i < g.dash_count; ++i)
    {
        paint_dash(c, dash_stroke_boundary(g.center, g.radius, g.stroke, g.dashes[i]), s.colour,
                   colour_a);
    }
    paint_disc(c, g.center.x, g.center.y, g.dot_radius, s.colour, colour_a);
    return c;
}

StraightImage to_straight(const PremulCanvas& c)
{
    StraightImage img;
    img.width = c.width;
    img.height = c.height;
    const std::size_t n = static_cast<std::size_t>(c.width) * c.height;
    img.rgba.assign(n * 4, 0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const float* p = &c.px[i * 4];
        const std::uint8_t a = to_byte(p[3]);
        if (a == 0) { continue; } // fully transparent: colour 0
        for (int k = 0; k < 3; ++k) { img.rgba[i * 4 + k] = to_byte(p[k] / p[3]); }
        img.rgba[i * 4 + 3] = a;
    }
    return img;
}

int radar_dash_reach(const CursorSettings& s)
{
    const float size = static_cast<float>(s.size_px);
    if (size <= 0.0f) { return 0; }
    const RadarDashGeometry g = radar_dash_geometry(0.0f, 0.0f, size, s.dash_count,
                                                    s.dash_width_pct);
    float reach = g.radius + 0.5f * g.outline_stroke;
    const float fill = size * kRadarDashFillExtentMax;
    if (fill > reach) { reach = fill; }
    return static_cast<int>(std::ceil(reach));
}

int combined_side(const CursorSettings& s, const PointerImage& p, bool* capped)
{
    int half = radar_dash_reach(s);
    const int w = p.image.width, h = p.image.height;
    for (int d : {p.hotspot_x, p.hotspot_y, w - p.hotspot_x, h - p.hotspot_y})
    {
        if (d > half) { half = d; }
    }
    const int want = 2 * half;
    const bool cut = want > kPointerPictureMaxSide;
    if (capped) { *capped = cut; }
    return cut ? kPointerPictureMaxSide : want;
}

CombinedPointer combine_pointer_with_radar_dash(const CursorSettings& s, const PointerImage& p)
{
    CombinedPointer out;
    const int side = combined_side(s, p, &out.capped);
    PremulCanvas c = paint_radar_dash(s, side);
    const int centre = side / 2;
    const int ox = centre - p.hotspot_x;
    const int oy = centre - p.hotspot_y;
    for (int y = 0; y < p.image.height; ++y)
    {
        const int cy = oy + y;
        if (cy < 0 || cy >= side) { continue; }
        for (int x = 0; x < p.image.width; ++x)
        {
            const int cx = ox + x;
            if (cx < 0 || cx >= side) { continue; }
            const std::uint8_t* src =
                &p.image.rgba[(static_cast<std::size_t>(y) * p.image.width + x) * 4];
            const float a = static_cast<float>(src[3]) / 255.0f;
            if (a <= 0.0f) { continue; }
            float* d = &c.px[(static_cast<std::size_t>(cy) * side + cx) * 4];
            const float keep = 1.0f - a;
            for (int k = 0; k < 3; ++k)
            {
                d[k] = static_cast<float>(src[k]) / 255.0f * a + d[k] * keep;
            }
            d[3] = a + d[3] * keep;
        }
    }
    out.image = to_straight(c);
    out.hotspot_x = centre;
    out.hotspot_y = centre;
    return out;
}

} // namespace cursor_finder
