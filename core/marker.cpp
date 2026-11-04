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

// The point at `angle` on the circle of `radius` about `c` (angle 0 at 3 o'clock,
// growing clockwise on screen).
MarkerPoint on_circle(const MarkerPoint& c, float radius, float angle)
{
    return MarkerPoint{c.x + radius * std::cos(angle), c.y + radius * std::sin(angle)};
}

// The angle of step k of `steps` from `from` to `to`, landing exactly on both ends.
float step_angle(float from, float to, int k, int steps)
{
    if (k == steps) { return to; }
    return from + (to - from) * static_cast<float>(k) / static_cast<float>(steps);
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

RadarDashGeometry radar_dash_geometry(float center_x, float center_y, float size,
                                      int dash_count, int dash_width_pct)
{
    if (size <= 0.0f) { return RadarDashGeometry{}; }
    // Design values in its 200-unit box: circle r 72, stroke 11, dot r 6; the
    // outline layer strokes the same circle at 17 with a dot r 9.
    constexpr float kDesignRadius           = 72.0f;
    constexpr float kDesignStroke           = 11.0f;
    constexpr float kDesignDotRadius        = 6.0f;
    constexpr float kDesignOutlineStroke    = 17.0f;
    constexpr float kDesignOutlineDotRadius = 9.0f;
    // The design's width scale ends at 0.75 (the dashes never join); its formula
    // divides by 0.9.
    constexpr float kDesignWidthMax         = 0.75f;
    constexpr float kDesignWidthDivisor     = 0.9f;
    const int n = std::clamp(dash_count, kRadarDashMinCount, kRadarDashMaxCount);
    const float t = static_cast<float>(std::clamp(dash_width_pct, 0, 100)) / 100.0f *
                    kDesignWidthMax;
    const float scale  = size / kDesignBox;
    const float period = 4.0f * kHalfPi / static_cast<float>(n); // as an angle
    // The centre line, cap centre to cap centre, in design units, then as half an
    // angle about the ring centre.
    const float period_len = period * kDesignRadius;
    const float body = (period_len - std::min(kDesignStroke, period_len)) * t /
                       kDesignWidthDivisor;
    const float half = 0.5f * body / kDesignRadius;
    RadarDashGeometry g;
    g.center = MarkerPoint{center_x, center_y};
    g.dash_count = n;
    for (int i = 0; i < n; ++i)
    {
        // Dash 0 is centred at 12 o'clock (angle -pi/2, y down); the rest follow
        // clockwise.
        const float mid = -kHalfPi + period * static_cast<float>(i);
        g.dashes[i].start = mid - half;
        g.dashes[i].end   = mid + half;
    }
    g.radius             = kDesignRadius * scale;
    g.stroke             = kDesignStroke * scale;
    g.dot_radius         = kDesignDotRadius * scale;
    // The outline scales with the marker as in the design, not the fixed band the
    // other presets use: a fixed 2 px band all round would close the gaps between
    // the dashes at small sizes.
    g.outline_stroke     = kDesignOutlineStroke * scale;
    g.outline_dot_radius = kDesignOutlineDotRadius * scale;
    return g;
}

DashBoundary dash_stroke_boundary(const MarkerPoint& center, float radius, float stroke,
                                  const DashArc& arc)
{
    DashBoundary out;
    if (radius <= 0.0f || stroke <= 0.0f) { return out; }
    const float h = stroke * 0.5f;
    const float pi = 2.0f * kHalfPi;
    out.fan_center = on_circle(center, radius, 0.5f * (arc.start + arc.end));
    auto push = [&](const MarkerPoint& p) {
        if (out.count < kDashBoundaryMaxPoints) { out.points[out.count++] = p; }
    };
    auto triangle = [&](int a, int b, int c) {
        if (out.triangle_count < kDashBoundaryMaxTriangles)
        {
            int* t = out.triangles[out.triangle_count++];
            t[0] = a; t[1] = b; t[2] = c;
        }
    };

    // No body (width 0): one round dot, a circle about the point from its outer
    // side, filled as a fan from its first point.
    if (arc.end <= arc.start)
    {
        constexpr int kDotPoints = 2 * kDashCapSteps;
        for (int k = 0; k < kDotPoints; ++k)
        {
            push(on_circle(out.fan_center, h,
                           arc.start + 2.0f * pi * static_cast<float>(k) /
                                           static_cast<float>(kDotPoints)));
        }
        for (int k = 1; k + 1 < kDotPoints; ++k) { triangle(0, k, k + 1); }
        return out;
    }

    // Walking by growing angle (about the ring centre on the outer side, about each
    // cap's centre round a cap) winds the polygon clockwise on screen.
    // 1. The outer side of the body, start to end, both corners included.
    for (int k = 0; k <= kDashArcSteps; ++k)
    {
        push(on_circle(center, radius + h, step_angle(arc.start, arc.end, k, kDashArcSteps)));
    }
    // 2. The end cap: a half circle about the body's end, from its outer corner
    //    (pointing away from the ring centre) to its inner corner. Only the points
    //    between; the corners belong to the sides.
    const MarkerPoint end = on_circle(center, radius, arc.end);
    for (int k = 1; k < kDashCapSteps; ++k)
    {
        push(on_circle(end, h, step_angle(arc.end, arc.end + pi, k, kDashCapSteps)));
    }
    // 3. The inner side of the body, end back to start, both corners included.
    for (int k = 0; k <= kDashArcSteps; ++k)
    {
        push(on_circle(center, radius - h, step_angle(arc.end, arc.start, k, kDashArcSteps)));
    }
    // 4. The start cap, from its inner corner round to its outer corner.
    const MarkerPoint start = on_circle(center, radius, arc.start);
    for (int k = 1; k < kDashCapSteps; ++k)
    {
        push(on_circle(start, h,
                       step_angle(arc.start + pi, arc.start + 2.0f * pi, k, kDashCapSteps)));
    }

    // The inside, over those points. Index of the outer side's point k steps from
    // the start, of the inner side's (which runs back from the end), and of each
    // cap's points between its corners (1 to kDashCapSteps - 1).
    constexpr int A = kDashArcSteps, C = kDashCapSteps;
    auto outer     = [&](int k) { return k; };
    auto end_cap   = [&](int j) { return A + j; };
    auto inner     = [&](int k) { return 2 * A + C - k; };
    auto start_cap = [&](int j) { return 2 * A + C + j; };
    // The body: a strip of quads, each cut in two, between the sides.
    for (int k = 0; k < A; ++k)
    {
        triangle(outer(k), outer(k + 1), inner(k + 1));
        triangle(outer(k), inner(k + 1), inner(k));
    }
    // Each cap, a half disc, as a fan from the corner it starts at.
    for (int j = 1; j < C - 1; ++j) { triangle(outer(A), end_cap(j), end_cap(j + 1)); }
    triangle(outer(A), end_cap(C - 1), inner(A));
    for (int j = 1; j < C - 1; ++j) { triangle(inner(0), start_cap(j), start_cap(j + 1)); }
    triangle(inner(0), start_cap(C - 1), outer(0));
    return out;
}

SoftHaloGeometry soft_halo_geometry(float size)
{
    if (size <= 0.0f) { return SoftHaloGeometry{}; }
    // Design values in its 200-unit box: disc r 96; alpha 0.92 at the centre, 0.5
    // at 52 % of the radius, 0 at the edge.
    constexpr float kDesignRadius = 96.0f;
    SoftHaloGeometry g;
    g.radius   = kDesignRadius * size / kDesignBox;
    g.stops[0] = SoftHaloStop{0.0f, 0.92f};
    g.stops[1] = SoftHaloStop{0.52f, 0.5f};
    g.stops[2] = SoftHaloStop{1.0f, 0.0f};
    return g;
}

} // namespace cursor_finder
