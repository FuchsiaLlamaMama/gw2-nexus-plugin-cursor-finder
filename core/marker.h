// cursor-finder-core — pointer-marker geometry. Pure C++17, no ImGui: the DLL feeds the
// returned floats straight into ImGui draw calls (AddImage / AddCircle / filled
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

} // namespace cursor_finder
