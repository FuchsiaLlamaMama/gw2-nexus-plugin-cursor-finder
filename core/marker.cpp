#include "core/marker.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cursor_finder {

namespace {

// The design box every preset SVG is drawn in (200 x 200 units, centre 100,100).
constexpr float kDesignBox    = 200.0f;
constexpr float kDesignCentre = 100.0f;
constexpr float kHalfPi       = 1.57079632679f;

// A design point (relative to the box centre) placed on screen.
MarkerPoint place(float dx, float dy, float center_x, float center_y, float scale)
{
    return MarkerPoint{center_x + dx * scale, center_y + dy * scale};
}

// A straight line through `p` along `d` (not normalised).
struct Line {
    MarkerPoint p;
    MarkerPoint d;
};

// The line through a -> b moved `by` pixels outward. For a shape wound clockwise on
// screen (y down) the outward normal is (dy, -dx) — the one ImGui's fill uses.
Line offset_edge(const MarkerPoint& a, const MarkerPoint& b, float by)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::hypot(dx, dy);
    const float nx = dy / len, ny = -dx / len;
    return Line{MarkerPoint{a.x + nx * by, a.y + ny * by}, MarkerPoint{dx, dy}};
}

// Where two non-parallel lines cross.
MarkerPoint intersect(const Line& l1, const Line& l2)
{
    const float cross = l1.d.x * l2.d.y - l1.d.y * l2.d.x;
    const float t = ((l2.p.x - l1.p.x) * l2.d.y - (l2.p.y - l1.p.y) * l2.d.x) / cross;
    return MarkerPoint{l1.p.x + l1.d.x * t, l1.p.y + l1.d.y * t};
}

} // namespace

MarkerRect centered_marker_rect(float center_x, float center_y, float size)
{
    if (size <= 0.0f)
    {
        // Degenerate: a zero-area rect at the point. Callers draw nothing; this
        // avoids an inverted (max < min) rectangle from a bad size.
        return MarkerRect{center_x, center_y, center_x, center_y};
    }
    const float half = size * 0.5f;
    return MarkerRect{center_x - half, center_y - half,
                      center_x + half, center_y + half};
}

PulseRingGeometry pulse_ring_geometry(float size)
{
    if (size <= 0.0f) { return PulseRingGeometry{}; }
    // Design values in its 200-unit box.
    constexpr float kDesignRadius = 74.0f;
    constexpr float kDesignStroke = 11.0f;
    const float scale = size / kDesignBox;
    PulseRingGeometry g;
    g.radius         = kDesignRadius * scale;
    g.colour_stroke  = kDesignStroke * scale;
    g.outline_stroke = g.colour_stroke + 2.0f * kPulseRingOutlineBandPx;
    return g;
}

CornerReticleGeometry corner_reticle_geometry(float center_x, float center_y, float size)
{
    if (size <= 0.0f) { return CornerReticleGeometry{}; }
    // Design values in its 200-unit box: the top-left bracket (40,64)->(40,40)->
    // (64,40) and its three mirrors, stroke 11, dot r 5.5.
    constexpr float kBrackets[4][3][2] = {
        {{40.0f, 64.0f},   {40.0f, 40.0f},   {64.0f, 40.0f}},
        {{136.0f, 40.0f},  {160.0f, 40.0f},  {160.0f, 64.0f}},
        {{160.0f, 136.0f}, {160.0f, 160.0f}, {136.0f, 160.0f}},
        {{64.0f, 160.0f},  {40.0f, 160.0f},  {40.0f, 136.0f}},
    };
    constexpr float kDesignStroke    = 11.0f;
    constexpr float kDesignDotRadius = 5.5f;
    const float scale = size / kDesignBox;
    CornerReticleGeometry g;
    for (int b = 0; b < 4; ++b)
    {
        for (int k = 0; k < 3; ++k)
        {
            g.brackets[b][k] = place(kBrackets[b][k][0] - kDesignCentre,
                                     kBrackets[b][k][1] - kDesignCentre,
                                     center_x, center_y, scale);
        }
    }
    g.dot_center         = MarkerPoint{center_x, center_y};
    g.stroke             = kDesignStroke * scale;
    g.dot_radius         = kDesignDotRadius * scale;
    g.outline_stroke     = g.stroke + 2.0f * kMarkerOutlineBandPx;
    g.outline_dot_radius = g.dot_radius + kMarkerOutlineBandPx;
    return g;
}

BracketBoundary bracket_stroke_boundary(const MarkerPoint (&bracket)[3], float stroke)
{
    BracketBoundary out;
    if (stroke <= 0.0f) { return out; }
    constexpr int steps = kBracketArcSteps;

    // A local frame on the corner vertex v: s along the arm to `a`, t along the arm
    // to `b`, with a -> b turning clockwise on screen, so walking the boundary by
    // increasing angle about v (s towards t) winds it clockwise.
    const MarkerPoint& v = bracket[1];
    MarkerPoint a = bracket[0];
    MarkerPoint b = bracket[2];
    if ((a.x - v.x) * (b.y - v.y) - (a.y - v.y) * (b.x - v.x) < 0.0f) { std::swap(a, b); }
    const float la = std::hypot(a.x - v.x, a.y - v.y);
    const float lb = std::hypot(b.x - v.x, b.y - v.y);
    if (la <= 0.0f || lb <= 0.0f) { return out; }
    const float ux = (a.x - v.x) / la, uy = (a.y - v.y) / la;
    const float wx = (b.x - v.x) / lb, wy = (b.y - v.y) / lb;
    const float h  = stroke * 0.5f;
    const float step = kHalfPi / static_cast<float>(steps);

    auto push = [&](float s, float t) {
        const MarkerPoint p{v.x + s * ux + t * wx, v.y + s * uy + t * wy};
        // Skip a repeated point (a zero-length edge has no normal to fringe along).
        if (out.count > 0 && std::fabs(out.points[out.count - 1].x - p.x) < 1e-5f &&
            std::fabs(out.points[out.count - 1].y - p.y) < 1e-5f)
        {
            return;
        }
        if (out.count < kBracketBoundaryMaxPoints) { out.points[out.count++] = p; }
    };

    // The boundary is the farther of the two arms' capsules in each direction from
    // v; the arm along s owns the directions up to the s = t diagonal, the arm
    // along t the rest. Both are h from v on the outside quadrant.
    // 1. Round outer corner: a quarter circle of radius h about v, (-h,0) -> (0,-h).
    for (int k = 0; k <= steps; ++k)
    {
        const float ang = 2.0f * kHalfPi + step * static_cast<float>(k);
        push(h * std::cos(ang), h * std::sin(ang));
    }
    // 2. The outer side of arm a, then its round cap (a half circle about a),
    //    up to the diagonal.
    for (int k = 0; k <= 2 * steps; ++k)
    {
        const float ang = -kHalfPi + step * static_cast<float>(k);
        const float s = la + h * std::cos(ang);
        const float t = h * std::sin(ang);
        if (t <= s) { push(s, t); }
    }
    // 3. The inner corner, on the diagonal: (h, h) where both inner sides meet; on
    //    an arm shorter than h (markers under ~22 px with the outline) the cap
    //    circles meet there instead.
    const float l = std::min(la, lb);
    const float inner = h <= l ? h : 0.5f * (l + std::sqrt(2.0f * h * h - l * l));
    push(inner, inner);
    // 4. Arm b's round cap from the diagonal, then its outer side back to step 1.
    for (int k = 0; k <= 2 * steps; ++k)
    {
        const float ang = step * static_cast<float>(k);
        const float s = h * std::cos(ang);
        const float t = lb + h * std::sin(ang);
        if (s <= t) { push(s, t); }
    }
    return out;
}

BeaconCrosshairGeometry beacon_crosshair_geometry(float center_x, float center_y, float size)
{
    if (size <= 0.0f) { return BeaconCrosshairGeometry{}; }
    // Design values in its 200-unit box, relative to the centre: the top triangle
    // (93,18),(107,18),(100,72) wound clockwise on screen, and dot r 5. The other
    // three are its rotations by 90 degrees (rotation keeps the winding).
    constexpr float kTop[3][2] = {{-7.0f, -82.0f}, {7.0f, -82.0f}, {0.0f, -28.0f}};
    constexpr float kDesignDotRadius = 5.0f;
    const float scale = size / kDesignBox;
    const float band  = kMarkerOutlineBandPx;
    BeaconCrosshairGeometry g;
    for (int t = 0; t < 4; ++t)
    {
        // Built about (0,0) and moved onto the centre last, to keep float error low.
        MarkerPoint tri[3];
        for (int k = 0; k < 3; ++k)
        {
            float x = kTop[k][0], y = kTop[k][1];
            for (int r = 0; r < t; ++r) // a quarter turn clockwise on screen
            {
                const float nx = -y;
                y = x;
                x = nx;
            }
            tri[k] = place(x, y, 0.0f, 0.0f, scale);
        }

        // The outline: each edge moved `band` outward; the two slanted sides would
        // meet ~15.6 px past the tip (a mitre), so the shape is cut straight across
        // the triangle's axis `band` past the colour tip instead.
        const Line base   = offset_edge(tri[0], tri[1], band);
        const Line side_1 = offset_edge(tri[1], tri[2], band);
        const Line side_2 = offset_edge(tri[2], tri[0], band);
        const float mx = (tri[0].x + tri[1].x) * 0.5f, my = (tri[0].y + tri[1].y) * 0.5f;
        const float axis_len = std::hypot(tri[2].x - mx, tri[2].y - my);
        const float ax = (tri[2].x - mx) / axis_len, ay = (tri[2].y - my) / axis_len;
        const Line cut{MarkerPoint{tri[2].x + ax * band, tri[2].y + ay * band},
                       MarkerPoint{-ay, ax}};
        const MarkerPoint shape[4] = {
            intersect(side_2, base), intersect(base, side_1),
            intersect(side_1, cut),  intersect(cut, side_2),
        };

        for (int k = 0; k < 3; ++k)
        {
            g.triangles[t][k] = MarkerPoint{center_x + tri[k].x, center_y + tri[k].y};
        }
        for (int k = 0; k < 4; ++k)
        {
            g.outline_shapes[t][k] = MarkerPoint{center_x + shape[k].x, center_y + shape[k].y};
        }
    }
    g.dot_center         = MarkerPoint{center_x, center_y};
    g.dot_radius         = kDesignDotRadius * scale;
    g.outline_dot_radius = g.dot_radius + kMarkerOutlineBandPx;
    return g;
}

} // namespace cursor_finder
