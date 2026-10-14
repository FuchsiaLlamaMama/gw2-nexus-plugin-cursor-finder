#include "core/marker.h"

namespace cursor_finder {

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
    constexpr float kDesignBox    = 200.0f;
    constexpr float kDesignRadius = 74.0f;
    constexpr float kDesignStroke = 11.0f;
    const float scale = size / kDesignBox;
    PulseRingGeometry g;
    g.radius         = kDesignRadius * scale;
    g.colour_stroke  = kDesignStroke * scale;
    g.outline_stroke = g.colour_stroke + 2.0f * kPulseRingOutlineBandPx;
    return g;
}

} // namespace cursor_finder
