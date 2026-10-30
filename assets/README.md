# Cursor Finder preset art

**All five presets are drawn in code** with the ImGui draw list, from the vector
design sources in [`./svg`](svg/). No preset uses an image, so every preset is
sharp at any size and draws from the first frame. Each `<preset>-colour.svg` is the
design source of that preset's shapes, drawn in a 200-unit box and scaled to the
marker size; the geometry lives in `core/marker.cpp` (tested off-game) and the
drawing in `src/entry.cpp`. Each `<preset>-outline.svg` is kept for reference
only: the drawn outline is a fixed 2 px dark band beside the colour, not the
design's scaled outline stroke. No game textures are bundled.

| Preset | Drawn by | Geometry | Shapes |
|--------|----------|----------|--------|
| Pulse Ring | `DrawPulseRing` | `pulse_ring_geometry` | a circle r 74, stroke 11 |
| Corner Reticle | `DrawCornerReticle` | `corner_reticle_geometry`, `bracket_stroke_boundary` | four brackets, stroke 11 with round caps and a round outer corner (each filled as one mesh), and a dot r 5.5 |
| Beacon Crosshair | `DrawBeaconCrosshair` | `beacon_crosshair_geometry` | four filled triangles and a dot r 5 |
| Radar Dash | `DrawRadarDash` | `radar_dash_geometry`, `dash_stroke_boundary`, `flat_dash_arc`, `flat_dash_boundary`, `dash_arc_points` | a dashed circle r 72, stroke 11 with round caps (flat ends with the outline on), and a dot r 6 |
| Soft Halo | `DrawSoftHalo` | `soft_halo_geometry` | a disc r 96 whose alpha falls from 0.92 at the centre to 0.5 at 52 % of the radius and 0 at the edge |

**Outlines.** The outline is drawn first, under the colour, in the outline
colour at 90 % of the marker's opacity:

- Pulse Ring and Corner Reticle: the same shapes stroked 2 px wider on each side.
- Beacon Crosshair: each triangle enlarged by 2 px on every side, its tip cut
  straight across 2 px past the colour tip.
- Radar Dash: with the outline on, each dash has flat ends instead of round ones,
  at the same visible length (each flat end sits where the round end reached).
  Two 2 px arcs per dash, one along its outer edge and one along its inner edge,
  span exactly that flat dash. They never cross the dash's ends and never lie
  under the colour, so the gaps between dashes stay open at every size and the
  dashes keep one even tone below full opacity. With the outline off, the ends
  stay round.
- Every centre dot: a dot 2 px larger.
- Soft Halo has no outline.

**Radar Dash's dashes are equal.** The design's dash pattern (26 on, 22 off) does
not fit its circle a whole number of times, which leaves the last dash shorter.
The drawn ring has 9 equal periods in the same 26 : 22 ratio instead, the first
dash starting at 3 o'clock and running clockwise, where the design's starts.

### Signature hues (colour defaults)

| Preset | Slug | Hue |
|--------|------|-----|
| Pulse Ring (default) | `pulse-ring` | magenta `#ff2d9b` |
| Corner Reticle | `corner-reticle` | cyan `#22e0ff` |
| Beacon Crosshair | `beacon-crosshair` | white `#f2f2f6` |
| Radar Dash | `radar-dash` | violet `#b26bff` |
| Soft Halo | `soft-halo` | teal `#3fd4c9` |

The style tiles show each preset's colour layer in its signature hue.

## Fill

The design has no fill geometry. The fill centre is a procedural translucent disc
(a square for the Corner Reticle) tinted by the Fill colour and drawn under the
marker. Off by default. Soft Halo has no fill: the halo is the fill.

## Other images

The quick-access icon, the preview pointer arrow and the window background are
images, not presets: PNGs in [`./textures`](textures/), embedded into the DLL as C
byte arrays (`icon_textures_data.h`, `icon_data.h`, `pointer_data.h`,
`window_bg_data.h`) and loaded via `Textures_GetOrCreateFromMemory`. Regenerate
the quick-access icon arrays with `python3 gen_icon_data.py`, run from this
folder.
