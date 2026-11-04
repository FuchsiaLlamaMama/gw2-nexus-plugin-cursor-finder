# Cursor Finder preset art

**All five presets are drawn in code** with the ImGui draw list, from the vector
design sources in [`./svg`](svg/). No preset uses an image, so every preset is
sharp at any size and draws from the first frame. Each `<preset>-colour.svg` is the
design source of that preset's shapes, drawn in a 200-unit box and scaled to the
marker size; the geometry lives in `core/marker.cpp` (tested off-game) and the
drawing in `src/entry.cpp`. Radar Dash's outline is drawn from its
`radar-dash-outline.svg`, scaled with the marker; the other presets'
`<preset>-outline.svg` files are kept for reference only, their drawn outline
being a fixed 2 px dark band beside the colour. No game textures are bundled.

| Preset | Drawn by | Geometry | Shapes |
|--------|----------|----------|--------|
| Pulse Ring | `DrawPulseRing` | `pulse_ring_geometry` | a circle r 74, stroke 11 |
| Corner Reticle | `DrawCornerReticle` | `corner_reticle_geometry`, `bracket_stroke_boundary` | four brackets, stroke 11 with round caps and a round outer corner (each filled as one mesh), and a dot r 5.5 |
| Beacon Crosshair | `DrawBeaconCrosshair` | `beacon_crosshair_geometry` | four filled triangles and a dot r 5 |
| Radar Dash | `DrawRadarDash` | `radar_dash_geometry`, `dash_stroke_boundary` | a dashed circle r 72, stroke 11 with round caps (each dash filled as one mesh), and a dot r 6 |
| Soft Halo | `DrawSoftHalo` | `soft_halo_geometry` | a disc r 96 whose alpha falls from 0.92 at the centre to 0.5 at 52 % of the radius and 0 at the edge |

**Outlines.** The outline is drawn first, under the colour, in the outline
colour at 90 % of the marker's opacity:

- Pulse Ring and Corner Reticle: the same shapes stroked 2 px wider on each side.
- Beacon Crosshair: each triangle enlarged by 2 px on every side, its tip cut
  straight across 2 px past the colour tip.
- Radar Dash: the design's outline layer, scaled with the marker like the
  colour, not a fixed 2 px band: the same round-capped dashes at stroke 17 and a
  dot r 9, so the outline reaches 3 units of the 200-unit design beyond the colour
  all around each dash and the dot (about 0.6 px at size 40, 1.5 px at 100). Each
  dash's outline is one mesh under the whole dash, so body and round ends keep
  one even tone below full opacity, and the gaps between the outlines stay open
  at every size. The dashes look the same with the outline on or off.
- Every other centre dot: a dot 2 px larger.
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
