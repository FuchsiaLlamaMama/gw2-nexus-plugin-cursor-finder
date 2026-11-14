// cursor-finder-core — the marker as a picture (spike 004-21, a throwaway probe).
//
// A small software rasteriser that paints the Radar Dash marker into an RGBA
// buffer with the same geometry and tints the DLL draws with ImGui, and an "over"
// composite of the game's pointer picture onto it. The DLL turns the result into a
// Windows pointer, so the system draws the marker with the pointer. Pure C++17,
// tested off-game.
#pragma once

#include <cstdint>
#include <vector>

#include "core/cursor_finder_settings.h"

namespace cursor_finder {

// The alpha (0-255) of each marker layer's tint, with the same integer arithmetic
// the DLL's LayerTint / OutlineTint / FillTint use.
int layer_alpha(int opacity_pct);                        // the colour layer
int outline_alpha(int opacity_pct);                      // the outline: x 0.9
int fill_alpha(int fill_opacity_pct, int opacity_pct);   // the fill disc

// The Radar Dash's fill disc at Fill size 100 %, as a fraction of the marker size
// (the DLL's preset table carries the same 0.35).
constexpr float kRadarDashFillExtentMax = 0.35f;

// The biggest pointer picture the probe makes, in pixels (Windows' usual limit).
constexpr int kPointerPictureMaxSide = 256;

// A picture with premultiplied float RGBA in 0-1, row by row from the top: four
// floats per pixel. The rasteriser composites in this form.
struct PremulCanvas {
    int width  = 0;
    int height = 0;
    std::vector<float> px;
};

// A picture with straight (not premultiplied) 8-bit RGBA, row by row from the top.
struct StraightImage {
    int width  = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// The game's pointer: its picture and its hotspot (the click pixel).
struct PointerImage {
    StraightImage image;
    int hotspot_x = 0;
    int hotspot_y = 0;
};

// The Radar Dash, painted on a transparent square of side `side`, centred on the
// point (side / 2, side / 2) (a pixel corner, as the marker centres on the
// pointer position in game), in DrawMarker's order: the fill disc (when on), the
// outline (when on), then the colour dashes and dot. 4 x 4 samples per pixel give
// the coverage; each shape (a dash, a dot, the disc) is blended over the picture
// on its own, as each is its own draw call in game.
PremulCanvas paint_radar_dash(const CursorSettings& s, int side);

// The picture with alpha un-premultiplied, rounded to 8 bits.
StraightImage to_straight(const PremulCanvas& c);

// How far the Radar Dash reaches from its centre at size `size_px`, in pixels,
// with the outline and the fill disc at their largest (rounded up).
int radar_dash_reach(const CursorSettings& s);

// The marker and the pointer combined: a square picture with straight alpha,
// whose hotspot is its centre.
struct CombinedPointer {
    StraightImage image;
    int hotspot_x = 0;
    int hotspot_y = 0;
    bool capped = false; // the picture needed more than kPointerPictureMaxSide
};

// The side a combined picture needs to hold the marker and the pointer with the
// hotspot at its centre: even, at most kPointerPictureMaxSide; `capped` says
// whether the cap cut it.
int combined_side(const CursorSettings& s, const PointerImage& p, bool* capped);

// The Radar Dash centred on the pointer's hotspot, with the pointer drawn over it
// ("over" blend: the pointer wins where it is opaque).
CombinedPointer combine_pointer_with_radar_dash(const CursorSettings& s,
                                                const PointerImage& p);

} // namespace cursor_finder
