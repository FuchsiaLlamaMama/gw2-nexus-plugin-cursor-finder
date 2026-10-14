// cursor-finder-core — pointer-marker geometry. Pure C++17, no ImGui: the DLL feeds the
// returned floats straight into ImGui draw calls (AddImage / AddCircle), but the
// math stays testable off-game.
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

// Width of the Pulse Ring's dark outline band on EACH side of the colour stroke,
// in pixels. Fixed — it does not grow with the marker (the design screens, not the
// exported 17-unit outline asset). Outline width (Thin / Medium / Thick) later
// changes only this value.
constexpr float kPulseRingOutlineBandPx = 2.0f;

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

} // namespace cursor_finder
