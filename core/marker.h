// cursor-finder-core — pointer-marker geometry. Pure C++17, no ImGui: the DLL feeds the
// returned floats straight into ImGui draw calls (AddCircle / polylines / filled
// polygons), but the math stays testable off-game.
#pragma once

namespace cursor_finder {

// An axis-aligned draw rectangle in screen pixels. Plain floats — deliberately
// NOT ImVec2, to keep ImGui out of cursor-finder-core. The DLL converts to ImVec2 at
// the draw site.
struct MarkerRect {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;

    float width()  const { return max_x - min_x; }
    float height() const { return max_y - min_y; }
    float center_x() const { return (min_x + max_x) * 0.5f; }
    float center_y() const { return (min_y + max_y) * 0.5f; }
};

// The square draw rectangle of side `size`, centered on (center_x, center_y).
// The marker is centered on the true click
// point (the OS cursor hotspot ImGui::GetMousePos() reports), never offset to a
// corner. `size` is the full width/height in pixels; a non-positive size yields a
// degenerate zero-area rect at the point (nothing to draw) rather than an
// inverted rectangle.
MarkerRect centered_marker_rect(float center_x, float center_y, float size);

// A screen-space point in pixels. Plain floats — not ImVec2 (see MarkerRect).
struct MarkerPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// Width of the dark outline band around every preset drawn in code, on EACH side
// of the colour shape, in pixels. Fixed — it does not grow with the marker (the
// design screens, not the exported outline assets, whose strokes scale). Outline
// width (Thin / Medium / Thick) later changes only this value.
constexpr float kMarkerOutlineBandPx = 2.0f;

// The Pulse Ring's outline band: the shared band.
constexpr float kPulseRingOutlineBandPx = kMarkerOutlineBandPx;

// The Pulse Ring drawn in code: one circle, stroked twice (outline under colour).
// All values in pixels; the stroke widths are full widths centred on `radius`.
struct PulseRingGeometry {
    float radius         = 0.0f; // stroke centre
    float colour_stroke  = 0.0f;
    float outline_stroke = 0.0f; // colour_stroke + 2 * kPulseRingOutlineBandPx
};

// The Pulse Ring's geometry at marker size `size`: the design's ring (200-unit
// box, circle radius 74, colour stroke 11; assets/svg/pulse-ring-colour.svg)
// scaled by size / 200, plus the fixed outline band. Returned so the live marker,
// the panel preview and the style tile size the ring identically. A non-positive
// size yields all zeros (nothing to draw).
PulseRingGeometry pulse_ring_geometry(float size);

// The Corner Reticle drawn in code: four L-shaped brackets stroked with round caps
// and a round outer corner, and a centre dot; the outline is the same shapes
// stroked 2 px wider on each side, drawn under the colour. Pixels, screen space.
struct CornerReticleGeometry {
    // Each bracket: {arm end, corner vertex, arm end}. Ordered top-left,
    // top-right, bottom-right, bottom-left, as the design lists them.
    MarkerPoint brackets[4][3];
    MarkerPoint dot_center;
    float stroke             = 0.0f; // colour stroke, full width
    float dot_radius         = 0.0f;
    float outline_stroke     = 0.0f; // stroke + 2 * kMarkerOutlineBandPx
    float outline_dot_radius = 0.0f; // dot_radius + kMarkerOutlineBandPx
};

// The Corner Reticle at marker size `size` centred on (center_x, center_y): the
// design's reticle (200-unit box; brackets (40,64)->(40,40)->(64,40) and its three
// mirrors, stroke 11, dot r 5.5; assets/svg/corner-reticle-colour.svg) scaled by
// size / 200, plus the fixed outline band. A non-positive size yields all zeros
// (nothing to draw).
CornerReticleGeometry corner_reticle_geometry(float center_x, float center_y, float size);

// Arc segments per quarter circle in a bracket's round caps and corner.
constexpr int kBracketArcSteps = 8;

// Most points in one bracket's stroke boundary: the outer corner's quarter circle
// (steps + 1), each arm's outer side and cap (up to 2 * steps + 1 each), and the
// inner corner.
constexpr int kBracketBoundaryMaxPoints = 5 * kBracketArcSteps + 4;

// One bracket's stroke boundary: the first `count` of `points`. Fixed capacity, so
// the DLL builds it every frame without touching the heap.
struct BracketBoundary {
    MarkerPoint points[kBracketBoundaryMaxPoints];
    int count = 0;
};

// The outer boundary of one bracket stroked at full width `stroke` with round caps
// and a round join (an SVG stroke-linejoin="round" corner): the closed polygon the
// DLL fills, wound clockwise on screen (y down) as ImGui's anti-aliased fill needs.
// Every point lies at stroke / 2 from the bracket's centre line. The polygon is
// star-shaped from the corner vertex (bracket[1]), so it fills as one triangle fan
// from there — the L is concave, so a fan from a boundary point would not do.
// Exact for a right-angled bracket whose arms are at least stroke / 2 long (the
// design's, down to a marker of ~22 px with the outline). Empty (count 0) for a
// non-positive stroke.
BracketBoundary bracket_stroke_boundary(const MarkerPoint (&bracket)[3], float stroke);

// The Beacon Crosshair drawn in code: four filled triangles pointing at the centre
// and a centre dot; the outline is each triangle enlarged by 2 px on every side,
// its tip cut straight across 2 px past the colour tip, and a dot 2 px larger,
// drawn under the colour. Pixels, screen space.
struct BeaconCrosshairGeometry {
    // Each triangle: {base corner, base corner, tip}, wound clockwise on screen.
    // Ordered top, right, bottom, left.
    MarkerPoint triangles[4][3];
    // Each enlarged triangle with its tip cut: {base corner, base corner, cut
    // corner, cut corner}, wound clockwise on screen; edge k runs parallel to the
    // matching colour edge, 2 px further out, and the cut runs across the axis.
    MarkerPoint outline_shapes[4][4];
    MarkerPoint dot_center;
    float dot_radius         = 0.0f;
    float outline_dot_radius = 0.0f; // dot_radius + kMarkerOutlineBandPx
};

// The Beacon Crosshair at marker size `size` centred on (center_x, center_y): the
// design's crosshair (200-unit box; triangle (93,18),(107,18),(100,72) and its
// rotations about (100,100), dot r 5; assets/svg/beacon-crosshair-colour.svg)
// scaled by size / 200, plus the fixed outline band. A non-positive size yields
// all zeros (nothing to draw).
BeaconCrosshairGeometry beacon_crosshair_geometry(float center_x, float center_y, float size);

// The Radar Dash's dashes. The design's dash pattern (26 on, 22 off) does not fit
// its circle a whole number of times, so the ring is drawn with 9 equal periods in
// the same 26 : 22 ratio.
constexpr int kRadarDashCount = 9;

// A dash's body, between its round caps, as angles in radians about the ring
// centre: 0 is 3 o'clock and a growing angle turns clockwise on screen (y down),
// the way an SVG circle's stroke runs. start < end.
struct DashArc {
    float start = 0.0f;
    float end   = 0.0f;
};

// The Radar Dash drawn in code: 9 arcs on one circle and a centre dot. With the
// outline off the arcs have round caps. With it on they have flat ends at the same
// visible length (flat_dash_arc), and the outline is a 2 px arc along each dash's
// outer and inner edge spanning exactly that (never across the ends, never under
// the colour) and a dot 2 px larger, drawn under the colour. Pixels, screen space.
struct RadarDashGeometry {
    MarkerPoint center;
    DashArc dashes[kRadarDashCount]; // the first starts at 3 o'clock
    float radius               = 0.0f; // the dashes' centre line
    float stroke               = 0.0f; // colour stroke, full width
    float dot_radius           = 0.0f;
    float outline_stroke       = 0.0f; // each outline arc's full width: kMarkerOutlineBandPx
    float outline_outer_radius = 0.0f; // centre of the arc along the outer edge
    float outline_inner_radius = 0.0f; // centre of the arc along the inner edge
    float outline_dot_radius   = 0.0f; // dot_radius + kMarkerOutlineBandPx
};

// The Radar Dash at marker size `size` centred on (center_x, center_y): the
// design's ring (200-unit box; circle r 72, stroke 11, round caps, dot r 6;
// assets/svg/radar-dash-colour.svg) scaled by size / 200, its dashes spread evenly
// as above, plus the fixed outline band. A non-positive size yields all zeros
// (nothing to draw).
RadarDashGeometry radar_dash_geometry(float center_x, float center_y, float size);

// Segments along one dash body, and round one cap (a half circle).
constexpr int kDashArcSteps = 16;
constexpr int kDashCapSteps = 12;

// Most points on one arc from dash_arc_points.
constexpr int kDashArcMaxPoints = kDashArcSteps + 1;

// Points along a circle of `radius` about `center` from arc.start to arc.end, both
// ends included: an outline arc's centre line, stroked open (flat ends). Fixed
// capacity, no heap. Empty (count 0) for a non-positive radius.
struct DashArcPoints {
    MarkerPoint points[kDashArcMaxPoints];
    int count = 0;
};
DashArcPoints dash_arc_points(const MarkerPoint& center, float radius, const DashArc& arc);

// Most points in one dash's stroke boundary: both long sides of the body and the
// inside of both caps.
constexpr int kDashBoundaryMaxPoints = 2 * (kDashArcSteps + 1) + 2 * (kDashCapSteps - 1);

// One dash's stroke boundary: the first `count` of `points`, and the point it is
// filled from. Fixed capacity, so the DLL builds it every frame without the heap.
struct DashBoundary {
    MarkerPoint fan_center;
    MarkerPoint points[kDashBoundaryMaxPoints];
    int count = 0;
};

// The outer boundary of the arc `arc` on the circle of `radius` about `center`,
// stroked at full width `stroke` with round caps: the closed polygon the DLL fills,
// wound clockwise on screen (y down). It runs along the outer side of the body,
// round the end cap, back along the inner side and round the start cap; each cap
// begins and ends exactly on a corner of the body. The shape is not convex but is
// star-shaped from the middle of its centre line (fan_center), so it fills as one
// triangle fan from there: one mesh, no seam where a cap meets the body, nothing
// painted twice. That holds while the inner side stays in view of the fan point,
// as it does for the Radar Dash at any size. Empty (count 0) for a non-positive
// radius or stroke.
DashBoundary dash_stroke_boundary(const MarkerPoint& center, float radius, float stroke,
                                  const DashArc& arc);

// The span a dash covers when drawn with flat ends (with the outline on): its body
// widened at both ends by the round cap's radius as an angle, stroke / 2 / radius,
// so each flat end sits where the round end reached and the dash keeps its visible
// length. The outline arcs span exactly this too. The dash unchanged for a
// non-positive radius or stroke.
DashArc flat_dash_arc(const DashArc& dash, float radius, float stroke);

// The outer boundary of the arc `arc` on the circle of `radius` about `center`,
// stroked at full width `stroke` with flat ends: the closed polygon the DLL fills,
// wound clockwise on screen (y down). It runs along the outer side, straight in
// along the end, back along the inner side and straight out along the start. Not
// convex, but star-shaped from the middle of its centre line (fan_center), so it
// fills as one triangle fan from there, nothing painted twice. That holds while
// the inner side stays in view of the fan point, half the arc less than
// acos((radius - stroke / 2) / radius): for the Radar Dash's flat dashes about
// 15.2 degrees against 22.5, at any size. Each side is cut into up to
// kDashArcSteps pieces, none shorter than a pixel, so the fill's soft edge does
// not fold back at the square corners of a small dash. Empty (count 0) for a
// non-positive radius or stroke.
DashBoundary flat_dash_boundary(const MarkerPoint& center, float radius, float stroke,
                                const DashArc& arc);

// The Soft Halo's alpha stops, from the centre out.
constexpr int kSoftHaloStops = 3;

// One stop of the halo's radial alpha: `at` is the fraction of the radius.
struct SoftHaloStop {
    float at    = 0.0f;
    float alpha = 0.0f;
};

// The Soft Halo drawn in code: a disc whose alpha falls off from the centre to
// nothing at its edge, linearly between the stops. No outline. Pixels.
struct SoftHaloGeometry {
    float radius = 0.0f;
    SoftHaloStop stops[kSoftHaloStops];
};

// The Soft Halo at marker size `size`: the design's glow (200-unit box; disc r 96,
// alpha 0.92 at the centre, 0.5 at 52 % of the radius, 0 at the edge;
// assets/svg/soft-halo-colour.svg) scaled by size / 200. A non-positive size
// yields a zero radius (nothing to draw).
SoftHaloGeometry soft_halo_geometry(float size);

} // namespace cursor_finder
