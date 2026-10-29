// cursor-finder-core unit tests (doctest) — the off-game, pure-logic surface of the
// Cursor Finder addon.
//
// These cover: settings defaults, JSON round-trip + write-through durability,
// schema versioning + forward migration (v1->v7), field clamping, colour hex
// round-trip, Reset-to-defaults, pointer geometry centering, the ClipFreeze
// (freeze-while-dragging) state machine, the combat/movement
// visibility matrix (is_in_combat, character-motion linger, should_show_marker,
// mode slug round-trip + migration), and the first-run `welcomed` flag
// (should_first_run_open, v6->v7 migration as already-welcomed for existing
// installs, Reset-preserves-welcomed). The Windows/ImGui surface (rendering,
// keybind, QuickAccess, panel, live preview, textures, the Win32 ClipCursor
// lifetime) is the manual in-game portion and is NOT asserted here.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/cursor_finder_store.h"
#include "core/marker.h"
#include "core/clip_freeze.h"
#include "core/visibility.h"
#include "persistence/atomic_file.h"
#include "theme/theme.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// A unique temp path per test, cleaned up on destruction.
struct TempStorePath {
    fs::path path;
    TempStorePath()
    {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("cursor-finder-core-test-" + std::to_string(counter++) + ".json");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempStorePath()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

std::string read_disk(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

// --- defaults on first run ----------------------------------------------------

TEST_CASE("a missing file yields the factory-default settings")
{
    TempStorePath tmp; // path does not exist
    REQUIRE_FALSE(fs::exists(tmp.path));

    cursor_finder::CursorStore store(tmp.path); // must not throw
    // Design defaults: finder ON, drawn above Nexus windows.
    CHECK(store.settings().enabled);
    CHECK(store.settings().draw_above_windows);
    CHECK(store.settings() == cursor_finder::CursorSettings::defaults());
}

// --- serialize -> write -> read -> deserialize round-trip ---------------------

TEST_CASE("settings round-trip through disk unchanged")
{
    TempStorePath tmp;
    {
        cursor_finder::CursorStore store(tmp.path);
        store.set_enabled(false);
        store.set_draw_above_windows(false);
    }
    // Fresh store on the same path == a next-session reload.
    cursor_finder::CursorStore reloaded(tmp.path);
    CHECK_FALSE(reloaded.settings().enabled);
    CHECK_FALSE(reloaded.settings().draw_above_windows);
}

// --- write-through — disk reflects a change with NO explicit flush ------------

TEST_CASE("mutations are written through to disk without an explicit flush")
{
    TempStorePath tmp;
    cursor_finder::CursorStore store(tmp.path);

    CHECK(store.set_enabled(false)); // returns true: value changed, no flush() call

    REQUIRE(fs::exists(tmp.path));
    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["enabled"] == false);

    CHECK(store.set_draw_above_windows(false));
    on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["draw_above_windows"] == false);

    // A no-op set (same value) reports no change.
    CHECK_FALSE(store.set_enabled(false));
}

// --- corrupt file -> graceful recovery to defaults (no throw) -----------------

TEST_CASE("a corrupt/unparseable file recovers to defaults without throwing")
{
    TempStorePath tmp;
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << "{ not valid json ]] :::";
    }

    cursor_finder::CursorStore store(tmp.path); // a throw would fail the run
    CHECK(store.settings() == cursor_finder::CursorSettings::defaults());

    // And the store is usable afterwards: a new write overwrites the garbage.
    store.set_enabled(false);
    cursor_finder::CursorStore reloaded(tmp.path);
    CHECK_FALSE(reloaded.settings().enabled);
}

// --- schema version present --------------------------------------------------

TEST_CASE("the persisted record carries a top-level schema version")
{
    TempStorePath tmp;
    cursor_finder::CursorStore store(tmp.path);
    store.set_enabled(false); // force a write

    json on_disk = json::parse(read_disk(tmp.path));
    REQUIRE(on_disk.contains("schema_version"));
    CHECK(on_disk["schema_version"] == cursor_finder::CursorSettings::kSchemaVersion);
}

// --- forward migration — unknown/extra fields load, absent default ------------

TEST_CASE("a later-schema file with extra fields loads forward-compatibly")
{
    TempStorePath tmp;
    // Simulate a file written by a *later* version: a bumped version, a field this
    // build has never heard of, and only `enabled`
    // set. The unknown field must be ignored, `enabled` honoured, and every
    // absent field fall back to its default — no data loss, no rejection.
    {
        json doc;
        doc["schema_version"]  = cursor_finder::CursorSettings::kSchemaVersion + 5;
        doc["enabled"]         = false;
        doc["future_matrix"]   = json::array();    // unknown here
        std::ofstream out(tmp.path, std::ios::binary);
        out << doc.dump(2);
    }

    cursor_finder::CursorStore store(tmp.path);
    CHECK_FALSE(store.settings().enabled);           // honoured
    CHECK(store.settings().draw_above_windows);      // absent -> default (true)
}

TEST_CASE("a pre-versioned file (no schema_version) migrates forward")
{
    TempStorePath tmp;
    // A hypothetical unversioned record: known fields present, no schema_version.
    // It must load its fields and be re-stamped at the current version on save.
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << R"({"enabled":false,"draw_above_windows":true})";
    }

    cursor_finder::CursorStore store(tmp.path);
    CHECK_FALSE(store.settings().enabled);
    CHECK(store.settings().draw_above_windows);

    // Any mutation re-stamps the file at the current schema version.
    store.set_enabled(true);
    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["schema_version"] == cursor_finder::CursorSettings::kSchemaVersion);
}

TEST_CASE("a malformed field degrades to its default, the rest loads")
{
    TempStorePath tmp;
    {
        // `enabled` wrong-typed (string) must degrade to the default, not throw;
        // `draw_above_windows` is a valid bool and is honoured.
        std::ofstream out(tmp.path, std::ios::binary);
        out << R"({"schema_version":1,"enabled":"yes","draw_above_windows":false})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().enabled);                 // malformed -> default (true)
    CHECK_FALSE(store.settings().draw_above_windows); // valid -> honoured
}

// --- appearance defaults ------------------------------------------------------

TEST_CASE("appearance factory defaults match the design")
{
    const cursor_finder::CursorSettings s = cursor_finder::CursorSettings::defaults();
    CHECK(s.preset == cursor_finder::Preset::PulseRing);          // default
    CHECK(s.colour == cursor_finder::signature_hue(cursor_finder::Preset::PulseRing));
    CHECK(s.colour == cursor_finder::Rgb{0xff, 0x2d, 0x9b});      // magenta
    CHECK(s.size_px == 96);                                 // design default
    CHECK(s.opacity_pct == 90);                             // design default
    CHECK(s.outline);                                       // on by default
    CHECK(s.outline_colour == cursor_finder::Rgb{0x14, 0x14, 0x18}); // dark default
    CHECK_FALSE(s.fill);                                    // off by default
    CHECK(s.fill_opacity_pct == 35);                        // design default
    CHECK(s.fill_colour == cursor_finder::Rgb{0xf2, 0xf2, 0xf6});  // near-white
    CHECK(s.fill_size_pct == 70);                           // v3: fill size %
    CHECK(s.show_quick_access_icon);                        // v5: on by default
    CHECK(s.out_of_combat_mode == cursor_finder::OutOfCombatMode::Always); // v6: shown by default
    CHECK(s.in_combat_mode == cursor_finder::InCombatMode::Always);        // v6: shown by default
    CHECK_FALSE(s.welcomed);                                        // v7: a fresh install is NOT yet welcomed
    // Schema bumped for the first-run welcomed flag (v7 added welcomed).
    CHECK(cursor_finder::CursorSettings::kSchemaVersion == 7);
}

// --- every appearance field round-trips through disk ---------------------------

TEST_CASE("every appearance field round-trips through disk")
{
    TempStorePath tmp;
    cursor_finder::CursorSettings edited = cursor_finder::CursorSettings::defaults();
    edited.preset           = cursor_finder::Preset::SoftHalo;
    edited.colour           = cursor_finder::Rgb{0x12, 0x34, 0x56};
    edited.size_px          = 88;  // within [kSizeMin, kSizeMax]
    edited.opacity_pct      = 55;
    edited.outline          = false;
    edited.outline_colour   = cursor_finder::Rgb{0xab, 0xcd, 0xef};
    edited.fill             = true;
    edited.fill_opacity_pct = 80;
    edited.fill_colour      = cursor_finder::Rgb{0x00, 0xff, 0x00};
    edited.fill_size_pct    = 45;
    edited.freeze_after_drag = true; // v4 field
    edited.show_quick_access_icon = false; // v5 field
    {
        cursor_finder::CursorStore store(tmp.path);
        CHECK(store.set(edited)); // write-through, value changed
    }
    cursor_finder::CursorStore reloaded(tmp.path);
    CHECK(reloaded.settings() == edited); // all fields preserved next session
}

TEST_CASE("appearance mutations are written through with no explicit flush")
{
    TempStorePath tmp;
    cursor_finder::CursorStore store(tmp.path);

    cursor_finder::CursorSettings next = store.settings();
    next.preset = cursor_finder::Preset::RadarDash;
    next.colour = cursor_finder::Rgb{0x0a, 0x0b, 0x0c};
    CHECK(store.set(next));

    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["preset"] == "radar_dash");
    CHECK(on_disk["colour"] == "#0a0b0c");
    CHECK(on_disk["schema_version"] == 7);
}

// --- the v1 -> v2 migration ---------------------------------------------------

TEST_CASE("a v1 file loads with appearance defaulted, then re-stamps v2")
{
    TempStorePath tmp;
    // Exactly what a v1 build wrote: schema 1, only the two v1 fields.
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << R"({"schema_version":1,"enabled":false,"draw_above_windows":false})";
    }
    cursor_finder::CursorStore store(tmp.path);
    // v1 fields honoured...
    CHECK_FALSE(store.settings().enabled);
    CHECK_FALSE(store.settings().draw_above_windows);
    // ...and every absent appearance field falls back to its default (no loss).
    CHECK(store.settings().preset == cursor_finder::Preset::PulseRing);
    CHECK(store.settings().colour == cursor_finder::Rgb{0xff, 0x2d, 0x9b});
    CHECK(store.settings().size_px == 96);
    CHECK(store.settings().outline);
    CHECK_FALSE(store.settings().fill);

    // Any mutation re-stamps the file at v2 with the appearance keys present.
    store.set_enabled(true);
    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["schema_version"] == 7);
    REQUIRE(on_disk.contains("preset"));
    CHECK(on_disk["preset"] == "pulse_ring");
    CHECK(on_disk["outline"] == true);
}

// --- malformed appearance fields degrade to defaults --------------------------

TEST_CASE("malformed appearance fields degrade to defaults, valid ones honoured")
{
    TempStorePath tmp;
    {
        // unknown preset slug, non-hex colour, wrong-typed size, valid opacity.
        std::ofstream out(tmp.path, std::ios::binary);
        out << R"({"schema_version":2,"preset":"laser_dorito","colour":"not-a-hex",)"
               R"("size_px":"big","opacity_pct":40})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().preset == cursor_finder::Preset::PulseRing);       // unknown -> default
    CHECK(store.settings().colour == cursor_finder::Rgb{0xff, 0x2d, 0x9b});   // bad hex -> default
    CHECK(store.settings().size_px == 96);                             // wrong type -> default
    CHECK(store.settings().opacity_pct == 40);                         // valid -> honoured
}

// --- out-of-range numbers are clamped on read --------------------------------

TEST_CASE("out-of-range size/opacity/fill-opacity are clamped on read")
{
    TempStorePath tmp;
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << R"({"schema_version":2,"size_px":5,"opacity_pct":0,"fill_opacity_pct":250,"fill_size_pct":3})";
    }
    cursor_finder::CursorStore lo(tmp.path);
    CHECK(lo.settings().size_px == cursor_finder::kSizeMin);          // 5 -> 40
    CHECK(lo.settings().opacity_pct == cursor_finder::kOpacityMin);   // 0 -> 20
    CHECK(lo.settings().fill_opacity_pct == cursor_finder::kFillOpacityMax); // 250 -> 100
    CHECK(lo.settings().fill_size_pct == cursor_finder::kFillSizeMin); // 3 -> 10

    {
        std::ofstream out(tmp.path, std::ios::binary);
        out << R"({"schema_version":2,"size_px":9999})";
    }
    cursor_finder::CursorStore hi(tmp.path);
    CHECK(hi.settings().size_px == cursor_finder::kSizeMax);          // 9999 -> 100
}

// --- Reset to defaults --------------------------------------------------------

TEST_CASE("reset to defaults restores the default appearance")
{
    TempStorePath tmp;
    cursor_finder::CursorStore store(tmp.path);

    cursor_finder::CursorSettings messed = store.settings();
    messed.preset = cursor_finder::Preset::BeaconCrosshair;
    messed.size_px = 100;
    messed.outline = false;
    messed.fill = true;
    CHECK(store.set(messed));

    // Reset == writing the factory defaults through the store (what the panel's
    // "Reset to defaults" button does).
    CHECK(store.set(cursor_finder::CursorSettings::defaults()));
    CHECK(store.settings() == cursor_finder::CursorSettings::defaults());

    cursor_finder::CursorStore reloaded(tmp.path); // and it persisted
    CHECK(reloaded.settings() == cursor_finder::CursorSettings::defaults());
}

// --- preset slug <-> enum round-trip ------------------------------------------

TEST_CASE("every preset slug round-trips through the enum")
{
    for (const cursor_finder::Preset p : {cursor_finder::Preset::PulseRing,
                                   cursor_finder::Preset::CornerReticle,
                                   cursor_finder::Preset::BeaconCrosshair,
                                   cursor_finder::Preset::RadarDash,
                                   cursor_finder::Preset::SoftHalo})
    {
        const auto back = cursor_finder::preset_from_slug(cursor_finder::preset_to_slug(p));
        REQUIRE(back.has_value());
        CHECK(*back == p);
    }
    CHECK_FALSE(cursor_finder::preset_from_slug("nope").has_value());
}

// --- pointer geometry centering ----------------------------------------------

TEST_CASE("the marker rect is centered on the point at several sizes")
{
    struct Case { float cx, cy, size; };
    for (const Case c : {Case{0, 0, 10}, Case{100, 200, 50},
                         Case{960, 540, 200}, Case{-30, 15, 64}})
    {
        const cursor_finder::MarkerRect r =
            cursor_finder::centered_marker_rect(c.cx, c.cy, c.size);
        // Centered on the click point.
        CHECK(r.center_x() == doctest::Approx(c.cx));
        CHECK(r.center_y() == doctest::Approx(c.cy));
        // Full side == size, square.
        CHECK(r.width()  == doctest::Approx(c.size));
        CHECK(r.height() == doctest::Approx(c.size));
        // Symmetric about the centre.
        CHECK(r.min_x == doctest::Approx(c.cx - c.size / 2));
        CHECK(r.max_x == doctest::Approx(c.cx + c.size / 2));
    }
}

TEST_CASE("a non-positive marker size yields a degenerate rect at the point")
{
    const cursor_finder::MarkerRect r = cursor_finder::centered_marker_rect(50, 60, 0);
    CHECK(r.width()  == doctest::Approx(0.0f));
    CHECK(r.height() == doctest::Approx(0.0f));
    CHECK(r.center_x() == doctest::Approx(50));
    CHECK(r.center_y() == doctest::Approx(60));
}

TEST_CASE("the pulse-ring geometry is the design's ring at size 200")
{
    // Design: 200-unit box, circle radius 74, colour stroke 11; the outline is the
    // colour stroke plus a fixed 2 px band on each side.
    const cursor_finder::PulseRingGeometry g = cursor_finder::pulse_ring_geometry(200);
    CHECK(g.radius         == doctest::Approx(74.0f));
    CHECK(g.colour_stroke  == doctest::Approx(11.0f));
    CHECK(g.outline_stroke == doctest::Approx(15.0f));
    CHECK(cursor_finder::kPulseRingOutlineBandPx == doctest::Approx(2.0f));
}

TEST_CASE("the pulse ring scales with size while the outline band stays 2 px")
{
    struct Case { float size; float radius; float colour_stroke; };
    const Case cases[] = {
        {40.0f,  14.8f,  2.2f},
        {74.0f,  27.38f, 4.07f},
        {100.0f, 37.0f,  5.5f},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.size);
        const cursor_finder::PulseRingGeometry g = cursor_finder::pulse_ring_geometry(c.size);
        CHECK(g.radius        == doctest::Approx(c.radius));
        CHECK(g.colour_stroke == doctest::Approx(c.colour_stroke));
        // Linear in size: radius and colour stroke keep the design's 74 : 11 : 200.
        CHECK(g.radius / c.size        == doctest::Approx(74.0f / 200.0f));
        CHECK(g.colour_stroke / c.size == doctest::Approx(11.0f / 200.0f));
        // The outline is 2 px beyond the colour stroke on each side, at every size.
        CHECK(g.outline_stroke == doctest::Approx(c.colour_stroke + 4.0f));
        CHECK((g.outline_stroke - g.colour_stroke) * 0.5f == doctest::Approx(2.0f));
    }
}

TEST_CASE("a non-positive marker size yields an empty pulse ring")
{
    for (const float size : {0.0f, -5.0f})
    {
        CAPTURE(size);
        const cursor_finder::PulseRingGeometry g = cursor_finder::pulse_ring_geometry(size);
        CHECK(g.radius         == doctest::Approx(0.0f));
        CHECK(g.colour_stroke  == doctest::Approx(0.0f));
        CHECK(g.outline_stroke == doctest::Approx(0.0f));
    }
}

// --- Corner Reticle / Beacon Crosshair drawn in code ---------------------------

namespace {
using cursor_finder::MarkerPoint;

// Marker sizes the drawn presets must hold at: the design box, the v1.0 slider's
// range, and the planned v1.1 minimum (3 % of 1080 px and 768 px screen height).
constexpr float kDrawnSizes[] = {200.0f, 100.0f, 74.0f, 40.0f, 32.0f, 23.0f};

// A design point (200-unit box, centre 100,100) placed on a marker of `size`
// centred on (cx, cy).
MarkerPoint design_point(float dx, float dy, float cx, float cy, float size)
{
    const float scale = size / 200.0f;
    return MarkerPoint{cx + (dx - 100.0f) * scale, cy + (dy - 100.0f) * scale};
}

void expect_point(const MarkerPoint& got, const MarkerPoint& want)
{
    CHECK(got.x == doctest::Approx(want.x));
    CHECK(got.y == doctest::Approx(want.y));
}

float distance(const MarkerPoint& a, const MarkerPoint& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Distance from p to the segment a-b.
float distance_to_segment(const MarkerPoint& p, const MarkerPoint& a, const MarkerPoint& b)
{
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float len2 = vx * vx + vy * vy;
    float t = len2 > 0.0f ? ((p.x - a.x) * vx + (p.y - a.y) * vy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return distance(p, MarkerPoint{a.x + t * vx, a.y + t * vy});
}

// Signed distance from p to the line through a -> b, positive on the OUTSIDE of
// a shape wound clockwise on screen (y down): the outward normal is (dy, -dx),
// the one ImGui's anti-aliased fill uses.
float outward_distance(const MarkerPoint& p, const MarkerPoint& a, const MarkerPoint& b)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::hypot(dx, dy);
    return ((p.x - a.x) * dy - (p.y - a.y) * dx) / len;
}

// Twice the shoelace area; positive means clockwise on screen (y down) — the
// winding ImGui 1.80's anti-aliased fill needs for its fringe to fall outside.
float twice_signed_area(const MarkerPoint* pts, int n)
{
    float a = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const MarkerPoint& p = pts[i];
        const MarkerPoint& q = pts[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return a;
}
} // namespace

TEST_CASE("the corner-reticle geometry is the design's reticle at size 200")
{
    // Design (assets/svg/corner-reticle-colour.svg): four brackets, stroke 11, round
    // caps and joins, centre dot r 5.5. Outline: stroke + 2 x 2 px, dot r + 2 px.
    const cursor_finder::CornerReticleGeometry g =
        cursor_finder::corner_reticle_geometry(100.0f, 100.0f, 200.0f);
    const float want[4][3][2] = {
        {{40, 64},   {40, 40},   {64, 40}},
        {{136, 40},  {160, 40},  {160, 64}},
        {{160, 136}, {160, 160}, {136, 160}},
        {{64, 160},  {40, 160},  {40, 136}},
    };
    for (int b = 0; b < 4; ++b)
    {
        CAPTURE(b);
        for (int k = 0; k < 3; ++k)
        {
            expect_point(g.brackets[b][k], MarkerPoint{want[b][k][0], want[b][k][1]});
        }
    }
    expect_point(g.dot_center, MarkerPoint{100.0f, 100.0f});
    CHECK(g.stroke             == doctest::Approx(11.0f));
    CHECK(g.dot_radius         == doctest::Approx(5.5f));
    CHECK(g.outline_stroke     == doctest::Approx(15.0f));
    CHECK(g.outline_dot_radius == doctest::Approx(7.5f));
    CHECK(cursor_finder::kMarkerOutlineBandPx == doctest::Approx(2.0f));
}

TEST_CASE("the corner reticle scales with size while the outline band stays 2 px")
{
    const float design[4][3][2] = {
        {{40, 64},   {40, 40},   {64, 40}},
        {{136, 40},  {160, 40},  {160, 64}},
        {{160, 136}, {160, 160}, {136, 160}},
        {{64, 160},  {40, 160},  {40, 136}},
    };
    const float cx = 640.0f, cy = 360.0f;
    for (const float size : kDrawnSizes)
    {
        CAPTURE(size);
        const cursor_finder::CornerReticleGeometry g =
            cursor_finder::corner_reticle_geometry(cx, cy, size);
        for (int b = 0; b < 4; ++b)
        {
            for (int k = 0; k < 3; ++k)
            {
                expect_point(g.brackets[b][k],
                             design_point(design[b][k][0], design[b][k][1], cx, cy, size));
            }
        }
        expect_point(g.dot_center, MarkerPoint{cx, cy});
        CHECK(g.stroke     == doctest::Approx(11.0f * size / 200.0f));
        CHECK(g.dot_radius == doctest::Approx(5.5f * size / 200.0f));
        // The outline is 2 px beyond the colour shape at every size.
        CHECK((g.outline_stroke - g.stroke) * 0.5f       == doctest::Approx(2.0f));
        CHECK(g.outline_dot_radius - g.dot_radius        == doctest::Approx(2.0f));
    }
}

namespace {
// The stroked bracket's boundary (what the DLL fills), checked against the bracket:
// every point lies at half the stroke from the bracket's centre line; in the
// corner's outside quadrant the points sit at half the stroke from the corner
// vertex (an SVG round join, not ImGui's square mitre at h * sqrt(2)); past each
// arm end, on that arm's side of the diagonal, they sit at half the stroke from the
// end (round caps). Wound clockwise on screen, and it fills as one triangle fan
// from the corner vertex with no two triangles overlapping. `eps` is the margin
// (px) for sorting points into the quadrant and past the caps.
void check_bracket_boundary(const MarkerPoint (&bracket)[3], float stroke, float eps)
{
    const float h = stroke * 0.5f;
    const MarkerPoint& a = bracket[0];
    const MarkerPoint& v = bracket[1];
    const MarkerPoint& e = bracket[2];
    const cursor_finder::BracketBoundary boundary =
        cursor_finder::bracket_stroke_boundary(bracket, stroke);
    REQUIRE(boundary.count >= 3);
    CHECK(boundary.count <= cursor_finder::kBracketBoundaryMaxPoints);
    CHECK(twice_signed_area(boundary.points, boundary.count) > 0.0f);

    // Unit directions from the corner vertex along each arm.
    const float la = distance(a, v), le = distance(e, v);
    const MarkerPoint ua{(a.x - v.x) / la, (a.y - v.y) / la};
    const MarkerPoint ue{(e.x - v.x) / le, (e.y - v.y) / le};
    int in_corner_quadrant = 0, past_cap_a = 0, past_cap_e = 0;
    for (int i = 0; i < boundary.count; ++i)
    {
        const MarkerPoint& p = boundary.points[i];
        const float along_a = (p.x - v.x) * ua.x + (p.y - v.y) * ua.y;
        const float along_e = (p.x - v.x) * ue.x + (p.y - v.y) * ue.y;
        const float to_line = std::min(distance_to_segment(p, a, v),
                                       distance_to_segment(p, v, e));
        CHECK(to_line == doctest::Approx(h).epsilon(1e-3));
        if (along_a < -eps && along_e < -eps)
        {
            ++in_corner_quadrant;
            CHECK(distance(p, v) == doctest::Approx(h).epsilon(1e-3));
        }
        if (along_a > la + eps && along_a >= along_e)
        {
            ++past_cap_a;
            CHECK(distance(p, a) == doctest::Approx(h).epsilon(1e-3));
        }
        if (along_e > le + eps && along_e >= along_a)
        {
            ++past_cap_e;
            CHECK(distance(p, e) == doctest::Approx(h).epsilon(1e-3));
        }
    }
    // The round pieces are really there (not just their end points).
    CHECK(in_corner_quadrant >= 3);
    CHECK(past_cap_a >= 3);
    CHECK(past_cap_e >= 3);

    // The DLL fills it as one triangle fan from the corner vertex. Every fan
    // triangle turns the same way and the fan goes round exactly once, so no two
    // triangles overlap: nothing is painted twice.
    const int n = boundary.count;
    float turned = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const MarkerPoint fan[3] = {v, boundary.points[i], boundary.points[(i + 1) % n]};
        CHECK(twice_signed_area(fan, 3) > 0.0f);
        const float p0x = fan[1].x - v.x, p0y = fan[1].y - v.y;
        const float p1x = fan[2].x - v.x, p1y = fan[2].y - v.y;
        turned += std::atan2(p0x * p1y - p0y * p1x, p0x * p1x + p0y * p1y);
    }
    CHECK(turned == doctest::Approx(2.0f * 3.14159265f).epsilon(1e-4));
}
} // namespace

TEST_CASE("a corner-reticle bracket has a round outer corner and round caps")
{
    for (const float size : kDrawnSizes)
    {
        const cursor_finder::CornerReticleGeometry g =
            cursor_finder::corner_reticle_geometry(300.0f, 200.0f, size);
        for (const float stroke : {g.stroke, g.outline_stroke})
        {
            for (int b = 0; b < 4; ++b)
            {
                CAPTURE(size);
                CAPTURE(stroke);
                CAPTURE(b);
                check_bracket_boundary(g.brackets[b], stroke, 1e-3f * size);
            }
        }
    }
}

TEST_CASE("a bracket with arms shorter than half its stroke stays round where its caps meet")
{
    // Under ~21.6 px the reticle's outline half-stroke (0.0275 * size + 2) outgrows
    // its arms (0.12 * size), and a short bracket stroked wide does the same: the
    // inner sides never reach each other, so the two caps' circles meet on the
    // diagonal instead, at (l + sqrt(2 h^2 - l^2)) / 2 along each arm — h from both
    // arm ends. The same properties hold as for a long-armed bracket.
    auto check_short = [](const MarkerPoint (&bracket)[3], float stroke) {
        const float h = stroke * 0.5f;
        const MarkerPoint& a = bracket[0];
        const MarkerPoint& v = bracket[1];
        const MarkerPoint& e = bracket[2];
        const float l = distance(a, v);
        REQUIRE(distance(e, v) == doctest::Approx(l));
        REQUIRE(l < h); // the short-arm case
        check_bracket_boundary(bracket, stroke, 1e-3f * stroke);

        // The caps' meeting point is on the boundary, on the diagonal past both ends.
        const float inner = 0.5f * (l + std::sqrt(2.0f * h * h - l * l));
        const MarkerPoint meet{v.x + inner * ((a.x - v.x) + (e.x - v.x)) / l,
                               v.y + inner * ((a.y - v.y) + (e.y - v.y)) / l};
        CHECK(distance(meet, a) == doctest::Approx(h).epsilon(1e-3));
        CHECK(distance(meet, e) == doctest::Approx(h).epsilon(1e-3));
        const cursor_finder::BracketBoundary boundary =
            cursor_finder::bracket_stroke_boundary(bracket, stroke);
        float nearest = stroke;
        for (int i = 0; i < boundary.count; ++i)
        {
            nearest = std::min(nearest, distance(boundary.points[i], meet));
        }
        CHECK(nearest < 1e-4f * stroke);
    };

    for (const float size : {21.0f, 16.0f, 10.0f})
    {
        const cursor_finder::CornerReticleGeometry g =
            cursor_finder::corner_reticle_geometry(300.0f, 200.0f, size);
        for (int b = 0; b < 4; ++b)
        {
            CAPTURE(size);
            CAPTURE(b);
            check_short(g.brackets[b], g.outline_stroke);
        }
    }
    // Arms 4 px and 1 px long under a 12 px stroke (h 6), well inside the case.
    for (const float l : {4.0f, 1.0f})
    {
        CAPTURE(l);
        const MarkerPoint bracket[3] = {{100.0f, 100.0f + l}, {100.0f, 100.0f}, {100.0f + l, 100.0f}};
        check_short(bracket, 12.0f);
    }
}

TEST_CASE("the beacon-crosshair geometry is the design's crosshair at size 200")
{
    // Design (assets/svg/beacon-crosshair-colour.svg): four filled triangles, the top
    // one (93,18),(107,18),(100,72) and its rotations about (100,100); dot r 5.
    // Ordered top, right, bottom, left; each base, base, tip, clockwise on screen.
    const cursor_finder::BeaconCrosshairGeometry g =
        cursor_finder::beacon_crosshair_geometry(100.0f, 100.0f, 200.0f);
    const float want[4][3][2] = {
        {{93, 18},   {107, 18},  {100, 72}},
        {{182, 93},  {182, 107}, {128, 100}},
        {{107, 182}, {93, 182},  {100, 128}},
        {{18, 107},  {18, 93},   {72, 100}},
    };
    for (int t = 0; t < 4; ++t)
    {
        CAPTURE(t);
        for (int k = 0; k < 3; ++k)
        {
            expect_point(g.triangles[t][k], MarkerPoint{want[t][k][0], want[t][k][1]});
        }
    }
    expect_point(g.dot_center, MarkerPoint{100.0f, 100.0f});
    CHECK(g.dot_radius         == doctest::Approx(5.0f));
    CHECK(g.outline_dot_radius == doctest::Approx(7.0f));
}

TEST_CASE("the beacon crosshair scales with size while the outline dot stays 2 px larger")
{
    const float design[4][3][2] = {
        {{93, 18},   {107, 18},  {100, 72}},
        {{182, 93},  {182, 107}, {128, 100}},
        {{107, 182}, {93, 182},  {100, 128}},
        {{18, 107},  {18, 93},   {72, 100}},
    };
    const float cx = 640.0f, cy = 360.0f;
    for (const float size : kDrawnSizes)
    {
        CAPTURE(size);
        const cursor_finder::BeaconCrosshairGeometry g =
            cursor_finder::beacon_crosshair_geometry(cx, cy, size);
        for (int t = 0; t < 4; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                expect_point(g.triangles[t][k],
                             design_point(design[t][k][0], design[t][k][1], cx, cy, size));
            }
        }
        expect_point(g.dot_center, MarkerPoint{cx, cy});
        CHECK(g.dot_radius                         == doctest::Approx(5.0f * size / 200.0f));
        CHECK(g.outline_dot_radius - g.dot_radius  == doctest::Approx(2.0f));
    }
}

TEST_CASE("each crosshair outline is its triangle enlarged 2 px, cut 2 px past the tip")
{
    // Tip half-angle: 7 units across, 54 long. The cut face (perpendicular to the
    // axis, 2 px past the tip) is 2 * (2 / cos a - 2 * tan a) ~= 3.52 px wide at
    // every size.
    const float half_angle = std::atan(7.0f / 54.0f);
    const float face_width =
        2.0f * (2.0f / std::cos(half_angle) - 2.0f * std::tan(half_angle));
    // Float coordinates near 400 px: allow ~1e-4 relative for the derived lengths.
    constexpr double kGeomEps = 1e-4;
    for (const float size : kDrawnSizes)
    {
        const cursor_finder::BeaconCrosshairGeometry g =
            cursor_finder::beacon_crosshair_geometry(400.0f, 300.0f, size);
        for (int t = 0; t < 4; ++t)
        {
            CAPTURE(size);
            CAPTURE(t);
            const MarkerPoint* tri = g.triangles[t];
            const MarkerPoint* q   = g.outline_shapes[t];
            // Both wound clockwise on screen (ImGui's anti-aliased fill needs it).
            CHECK(twice_signed_area(tri, 3) > 0.0f);
            CHECK(twice_signed_area(q, 4) > 0.0f);
            // And convex (AddConvexPolyFilled fans it from its first point): it turns
            // the same way, clockwise, at every corner.
            for (int k = 0; k < 4; ++k)
            {
                CAPTURE(k);
                const MarkerPoint corner[3] = {q[(k + 3) % 4], q[k], q[(k + 1) % 4]};
                CHECK(twice_signed_area(corner, 3) > 0.0f);
            }

            // The base is 2 px further out.
            CHECK(outward_distance(q[0], tri[0], tri[1]) == doctest::Approx(2.0f).epsilon(kGeomEps));
            CHECK(outward_distance(q[1], tri[0], tri[1]) == doctest::Approx(2.0f).epsilon(kGeomEps));
            // Each slanted side is 2 px further out along its whole length.
            CHECK(outward_distance(q[1], tri[1], tri[2]) == doctest::Approx(2.0f).epsilon(kGeomEps));
            CHECK(outward_distance(q[2], tri[1], tri[2]) == doctest::Approx(2.0f).epsilon(kGeomEps));
            CHECK(outward_distance(q[3], tri[2], tri[0]) == doctest::Approx(2.0f).epsilon(kGeomEps));
            CHECK(outward_distance(q[0], tri[2], tri[0]) == doctest::Approx(2.0f).epsilon(kGeomEps));

            // The tip cut: both cut corners lie 2 px past the colour tip along the
            // triangle's axis (so the cut is perpendicular to it).
            const MarkerPoint mid{(tri[0].x + tri[1].x) * 0.5f, (tri[0].y + tri[1].y) * 0.5f};
            const float axis_len = distance(mid, tri[2]);
            const MarkerPoint axis{(tri[2].x - mid.x) / axis_len, (tri[2].y - mid.y) / axis_len};
            for (const MarkerPoint& cut : {q[2], q[3]})
            {
                const float past_tip = (cut.x - tri[2].x) * axis.x + (cut.y - tri[2].y) * axis.y;
                CHECK(past_tip == doctest::Approx(2.0f).epsilon(kGeomEps));
            }
            CHECK(distance(q[2], q[3]) == doctest::Approx(face_width).epsilon(kGeomEps));
        }
    }
}

TEST_CASE("a non-positive marker size yields an empty reticle and crosshair")
{
    for (const float size : {0.0f, -5.0f})
    {
        CAPTURE(size);
        const cursor_finder::CornerReticleGeometry r =
            cursor_finder::corner_reticle_geometry(50.0f, 60.0f, size);
        CHECK(r.stroke             == doctest::Approx(0.0f));
        CHECK(r.dot_radius         == doctest::Approx(0.0f));
        CHECK(r.outline_stroke     == doctest::Approx(0.0f));
        CHECK(r.outline_dot_radius == doctest::Approx(0.0f));
        CHECK(cursor_finder::bracket_stroke_boundary(r.brackets[0], r.stroke).count == 0);

        const cursor_finder::BeaconCrosshairGeometry c =
            cursor_finder::beacon_crosshair_geometry(50.0f, 60.0f, size);
        CHECK(c.dot_radius         == doctest::Approx(0.0f));
        CHECK(c.outline_dot_radius == doctest::Approx(0.0f));
        for (int t = 0; t < 4; ++t)
        {
            CHECK(twice_signed_area(c.triangles[t], 3)      == doctest::Approx(0.0f));
            CHECK(twice_signed_area(c.outline_shapes[t], 4) == doctest::Approx(0.0f));
        }
    }
}

// --- clip state machine test helper -------------------------------------------

namespace {
// Assert a returned draw position.
void expect_at(cursor_finder::Vec2 got, float x, float y)
{
    CHECK(got.x == doctest::Approx(x));
    CHECK(got.y == doctest::Approx(y));
}
} // namespace

TEST_CASE("a v3 file (no freeze_after_drag) migrates forward to v4 with the default")
{
    TempStorePath tmp;
    {
        std::ofstream out(tmp.path);
        out << R"({"schema_version":3,"enabled":true,"draw_above_windows":true,)"
               R"("preset":"pulse_ring","colour":"#ff2d9b","size_px":96,)"
               R"("opacity_pct":90,"outline":true,"outline_colour":"#141418",)"
               R"("fill":false,"fill_opacity_pct":35,"fill_colour":"#f2f2f6",)"
               R"("fill_size_pct":70})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().freeze_after_drag == false); // absent -> default

    // Re-stamped at v4 with the field present on the next write-through.
    cursor_finder::CursorSettings next = store.settings();
    next.freeze_after_drag = true;
    CHECK(store.set(next));
    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["schema_version"] == 7); // re-stamped at the current version
    CHECK(on_disk["freeze_after_drag"] == true);
}

TEST_CASE("a v4 file (no show_quick_access_icon) migrates forward to v5 with the default")
{
    TempStorePath tmp;
    {
        // Exactly what a v4 build wrote: schema 4, every field up to freeze_after_drag,
        // but no show_quick_access_icon.
        std::ofstream out(tmp.path);
        out << R"({"schema_version":4,"enabled":true,"draw_above_windows":true,)"
               R"("preset":"pulse_ring","colour":"#ff2d9b","size_px":96,)"
               R"("opacity_pct":90,"outline":true,"outline_colour":"#141418",)"
               R"("fill":false,"fill_opacity_pct":35,"fill_colour":"#f2f2f6",)"
               R"("fill_size_pct":70,"freeze_after_drag":false})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().show_quick_access_icon == true); // absent -> default on

    // Re-stamped at v5 with the field present on the next write-through, and a
    // toggle to false survives a reload (the change survives a restart).
    cursor_finder::CursorSettings next = store.settings();
    next.show_quick_access_icon = false;
    CHECK(store.set(next));
    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["schema_version"] == 7);
    CHECK(on_disk["show_quick_access_icon"] == false);

    cursor_finder::CursorStore reloaded(tmp.path);
    CHECK(reloaded.settings().show_quick_access_icon == false);
}

// --- clip-cursor-while-dragging state machine ---------------------------------
//
// The freeze is level-based: it pins the pointer while a button is held AND the OS
// cursor is hidden (an in-world drag) AND addon UI isn't capturing — re-evaluated
// every frame, released when any of those stops or the button lifts. The
// default cursor_hidden=true in these basic cases models an in-world press.
// Letting go of the drag IS the release — no hotkey, no resume-on-movement.

TEST_CASE("clip: toggle OFF is a pass-through: never freezes")
{
    cursor_finder::ClipFreeze f;
    expect_at(f.update(false, true,  {100, 100}), 100, 100);
    expect_at(f.update(false, true,  {140, 100}), 140, 100);
    expect_at(f.update(false, false, {140, 100}), 140, 100);
    CHECK_FALSE(f.frozen());
}

TEST_CASE("clip: button-down freezes the pointer at the press point")
{
    cursor_finder::ClipFreeze f;
    expect_at(f.update(true, true, {100, 100}), 100, 100); // press => freeze here
    CHECK(f.frozen());
    CHECK(f.frozen_pos().x == doctest::Approx(100));
    CHECK(f.frozen_pos().y == doctest::Approx(100));
}

TEST_CASE("clip: the pointer stays pinned at the press point for the whole hold")
{
    cursor_finder::ClipFreeze f;
    f.update(true, true, {100, 100}); // press at (100,100)
    REQUIRE(f.frozen());
    // The live pointer moving while held must NOT move the marker: it stays pinned
    // at the press point (mirrors the OS clip holding the cursor there).
    expect_at(f.update(true, true, {130, 100}), 100, 100);
    expect_at(f.update(true, true, {400, 400}), 100, 100);
    CHECK(f.frozen());
}

TEST_CASE("clip: button-up releases the freeze — letting go IS the release")
{
    cursor_finder::ClipFreeze f;
    f.update(true, true, {100, 100}); // press
    f.update(true, true, {130, 100}); // hold
    REQUIRE(f.frozen());
    // Button up: unfreeze and track the live pointer again.
    expect_at(f.update(true, false, {130, 100}), 130, 100);
    CHECK_FALSE(f.frozen());
}

TEST_CASE("clip: a quick click freezes only for the held frame(s)")
{
    cursor_finder::ClipFreeze f;
    expect_at(f.update(true, true,  {100, 100}), 100, 100); // press (frozen)
    CHECK(f.frozen());
    expect_at(f.update(true, false, {100, 100}), 100, 100); // release
    CHECK_FALSE(f.frozen());
}

TEST_CASE("clip: a fresh press after release re-freezes at the NEW press point")
{
    cursor_finder::ClipFreeze f;
    f.update(true, true,  {100, 100});
    f.update(true, false, {100, 100}); // released
    REQUIRE_FALSE(f.frozen());
    expect_at(f.update(true, true, {250, 260}), 250, 260); // new press
    CHECK(f.frozen());
    CHECK(f.frozen_pos().x == doctest::Approx(250));
    CHECK(f.frozen_pos().y == doctest::Approx(260));
}

TEST_CASE("clip: turning the toggle off mid-hold releases the pointer")
{
    cursor_finder::ClipFreeze f;
    f.update(true, true, {100, 100});
    f.update(true, true, {130, 100});
    REQUIRE(f.frozen());
    expect_at(f.update(false, true, {130, 100}), 130, 100); // toggle off, still held
    CHECK_FALSE(f.frozen());
}

// A press captured by another addon's ImGui window must NOT freeze the
// pointer, or that window can't be dragged (moved, sliders, scrollbars). The
// freeze is for in-world clicks only.
TEST_CASE("clip: a press captured by addon UI does NOT freeze — UI drags stay free")
{
    cursor_finder::ClipFreeze f;
    // Press lands on addon UI (WantCaptureMouse == true): no freeze.
    expect_at(f.update(true, true, {100, 100}, /*ui_capturing=*/true), 100, 100);
    CHECK_FALSE(f.frozen());
    // Dragging the window: the live pointer is tracked, never pinned.
    expect_at(f.update(true, true, {140, 120}, /*ui_capturing=*/true), 140, 120);
    CHECK_FALSE(f.frozen());
    expect_at(f.update(true, true, {180, 160}, /*ui_capturing=*/true), 180, 160);
    CHECK_FALSE(f.frozen());
    // Release stays free too.
    expect_at(f.update(true, false, {180, 160}, /*ui_capturing=*/true), 180, 160);
    CHECK_FALSE(f.frozen());
}

// Addon UI is excluded even when the cursor is hidden — the belt-and-
// braces WantCaptureMouse guard wins, so an addon window drag never freezes.
TEST_CASE("clip: addon UI is never frozen, even with the cursor hidden")
{
    cursor_finder::ClipFreeze f;
    expect_at(f.update(true, true, {100, 100}, /*ui_capturing=*/true, /*cursor_hidden=*/true),
              100, 100);
    CHECK_FALSE(f.frozen());
    expect_at(f.update(true, true, {140, 120}, /*ui_capturing=*/true, /*cursor_hidden=*/true),
              140, 120);
    CHECK_FALSE(f.frozen());
}

// The freeze must only engage for an in-world camera/character drag,
// detected by the OS cursor being hidden. A press on a GW2 native window (title
// bar / resize corner) keeps the cursor SHOWN, so it must NOT freeze — otherwise
// the window can't be moved or resized.
TEST_CASE("clip: an in-world drag (cursor hidden) still freezes")
{
    cursor_finder::ClipFreeze f;
    // Cursor hidden = GW2 in-world camera/character drag: freeze as designed.
    expect_at(f.update(true, true, {100, 100}, /*ui_capturing=*/false, /*cursor_hidden=*/true),
              100, 100);
    CHECK(f.frozen());
    expect_at(f.update(true, true, {140, 130}, false, /*cursor_hidden=*/true), 100, 100);
    CHECK(f.frozen());
}

TEST_CASE("clip: a native-window press (cursor shown) does NOT freeze — windows drag")
{
    cursor_finder::ClipFreeze f;
    // Cursor shown = press is over window chrome (native title bar / resize corner):
    // never freeze, so the window follows the pointer.
    expect_at(f.update(true, true, {100, 100}, /*ui_capturing=*/false, /*cursor_hidden=*/false),
              100, 100);
    CHECK_FALSE(f.frozen());
    expect_at(f.update(true, true, {160, 140}, false, /*cursor_hidden=*/false), 160, 140);
    CHECK_FALSE(f.frozen());
    expect_at(f.update(true, false, {160, 140}, false, /*cursor_hidden=*/false), 160, 140);
    CHECK_FALSE(f.frozen());
}

// The core regression: GW2 hides the cursor a frame or two AFTER the
// press, once it registers the drag — so the freeze must be re-checked every frame,
// not latched at the press. A left-drag that starts with the cursor shown and hides
// mid-hold MUST freeze the moment it hides. (A press-edge-only check saw "shown" on
// frame 1 and never froze, so a character turn was never frozen.)
TEST_CASE("clip: left-drag freezes once the cursor hides mid-hold, not just at press")
{
    cursor_finder::ClipFreeze f;
    // Frame 1: button just pressed, GW2 hasn't hidden the cursor yet.
    expect_at(f.update(true, true, {100, 100}, false, /*cursor_hidden=*/false), 100, 100);
    CHECK_FALSE(f.frozen());
    // Frame 2: GW2 registers the drag and hides the cursor -> freeze engages now.
    expect_at(f.update(true, true, {120, 110}, false, /*cursor_hidden=*/true), 120, 110);
    CHECK(f.frozen());
    // Stays pinned for the rest of the hidden hold.
    expect_at(f.update(true, true, {400, 400}, false, /*cursor_hidden=*/true), 120, 110);
    CHECK(f.frozen());
    // Button up releases.
    expect_at(f.update(true, false, {400, 400}, false, /*cursor_hidden=*/true), 400, 400);
    CHECK_FALSE(f.frozen());
}

// --- combat & movement visibility --------------------------------------------
//
// The pure decision logic behind the "Show overlay" matrix: the combat-bit
// predicate over MumbleContext.UiState, the out-of-combat "While moving"
// character-motion linger, and the should_show_marker matrix (five cells +
// master-off + out-of-gameplay).
// The MumbleLink read + per-frame ImGui plumbing is the Windows/in-game surface,
// not asserted here.

TEST_CASE("is_in_combat reads UiState bit 6 / 0x40")
{
    CHECK(cursor_finder::is_in_combat(0x40u));           // the combat bit alone
    CHECK_FALSE(cursor_finder::is_in_combat(0x00u));      // nothing set
    // Neighbouring bits must not false-trigger combat.
    CHECK_FALSE(cursor_finder::is_in_combat(0x01u));      // IsMapOpen (bit 0)
    CHECK_FALSE(cursor_finder::is_in_combat(0x10u));      // IsInCompetitiveGamemode
    CHECK_FALSE(cursor_finder::is_in_combat(0xFFFFFFFFu & ~0x40u)); // every bit but combat
    // The combat bit reads set even alongside other flags.
    CHECK(cursor_finder::is_in_combat(0x41u));            // combat + map-open
    CHECK(cursor_finder::is_in_combat(0xFFFFFFFFu));      // all bits
}

TEST_CASE("MotionLinger: character motion shows; stillness past the linger hides")
{
    cursor_finder::MotionLinger m; // default: 1000 ms linger
    // Standing still from the first frame -> hidden (nothing has moved yet).
    CHECK_FALSE(m.update(false, 0));
    CHECK_FALSE(m.update(false, 500));
    // Nexus reports IsMoving -> shown.
    CHECK(m.update(true, 1000));
    // Stopped, still inside the 1 s linger -> shown.
    CHECK(m.update(false, 1300));
    CHECK(m.update(false, 1999));
    // Stopped for 1 s or more -> hidden.
    CHECK_FALSE(m.update(false, 2000));
    CHECK_FALSE(m.update(false, 5000));
    // Moving again -> shown.
    CHECK(m.update(true, 5016));
}

TEST_CASE("MotionLinger: a single-frame gap inside a walk does not hide the marker")
{
    cursor_finder::MotionLinger m;
    for (int t = 0; t <= 2000; t += 16)
    {
        const bool gap = (t == 800); // one false frame mid-walk
        CHECK(m.update(!gap, t));
    }
}

TEST_CASE("MotionLinger: sparse 100 ms IsMoving pulses stay shown (low-FPS worst case)")
{
    // Nexus recomputes IsMoving every 100 ms. At very low FPS a window can miss
    // a position write: model true for one 100 ms window, then false for up to
    // 300 ms, repeated for 5 s. Sampled every 16 ms, the marker never drops.
    cursor_finder::MotionLinger m;
    for (int t = 0; t < 5000; t += 16)
    {
        const bool moving = (t % 400) < 100;
        CHECK(m.update(moving, t));
    }
}

TEST_CASE("MotionLinger: a clock that jumps backward reads as no recent move")
{
    cursor_finder::MotionLinger m;
    CHECK(m.update(true, 10000));
    // The timestamp goes back (e.g. the clock was reset): not a recent move.
    CHECK_FALSE(m.update(false, 500));
}

TEST_CASE("should_show_marker: the master-off switch hides regardless")
{
    cursor_finder::CursorSettings s;
    s.enabled = false;
    CHECK_FALSE(cursor_finder::should_show_marker(s, true,  false, true));
    CHECK_FALSE(cursor_finder::should_show_marker(s, true,  true,  true));
    // Master-off hides outside gameplay too.
    CHECK_FALSE(cursor_finder::should_show_marker(s, false, false, false));
}

TEST_CASE("should_show_marker: outside gameplay hides the marker with the settings window closed")
{
    // Char select / loading screens: Nexus reports !IsGameplay. The marker must
    // stay hidden whatever the matrix says — even the always-show cells — while
    // the settings window is closed (the open-window carve-out is tested below).
    cursor_finder::CursorSettings s;
    s.enabled            = true;
    s.out_of_combat_mode = cursor_finder::OutOfCombatMode::Always;
    s.in_combat_mode     = cursor_finder::InCombatMode::Always;
    CHECK_FALSE(cursor_finder::should_show_marker(s, /*in_gameplay=*/false, false, false, /*settings_open=*/false));
    CHECK_FALSE(cursor_finder::should_show_marker(s, /*in_gameplay=*/false, false, true, /*settings_open=*/false));
    CHECK_FALSE(cursor_finder::should_show_marker(s, /*in_gameplay=*/false, true,  false, /*settings_open=*/false));
    // Back in gameplay, the same settings show again.
    CHECK(cursor_finder::should_show_marker(s, /*in_gameplay=*/true, false, false));
    CHECK(cursor_finder::should_show_marker(s, /*in_gameplay=*/true, true,  false));
}

TEST_CASE("should_show_marker: outside gameplay shows only while the settings window is open")
{
    // The first-run settings window usually opens at char select: while it is
    // open the pointer marker shows there, even with both columns on Never, so
    // edits are visible at the pointer. Closing it hides the marker again.
    cursor_finder::CursorSettings s;
    s.enabled            = true;
    s.out_of_combat_mode = cursor_finder::OutOfCombatMode::Never;
    s.in_combat_mode     = cursor_finder::InCombatMode::Never;
    CHECK(cursor_finder::should_show_marker(s, /*in_gameplay=*/false, false, false, /*settings_open=*/true));
    CHECK_FALSE(cursor_finder::should_show_marker(s, /*in_gameplay=*/false, false, false, /*settings_open=*/false));
    // Master-off still wins.
    s.enabled = false;
    CHECK_FALSE(cursor_finder::should_show_marker(s, /*in_gameplay=*/false, false, false, /*settings_open=*/true));
    // In gameplay the matrix decides as before; an open window doesn't override it.
    s.enabled = true;
    CHECK_FALSE(cursor_finder::should_show_marker(s, /*in_gameplay=*/true, false, false, /*settings_open=*/true));
}

TEST_CASE("should_show_marker: out-of-combat column Always/WhileMoving/Never")
{
    cursor_finder::CursorSettings s;
    s.enabled            = true;
    const bool in_combat = false;

    s.out_of_combat_mode = cursor_finder::OutOfCombatMode::Always;
    CHECK(cursor_finder::should_show_marker(s, true, in_combat, false));
    CHECK(cursor_finder::should_show_marker(s, true, in_combat, true));

    s.out_of_combat_mode = cursor_finder::OutOfCombatMode::WhileMoving;
    CHECK_FALSE(cursor_finder::should_show_marker(s, true, in_combat, false)); // still -> hidden
    CHECK(cursor_finder::should_show_marker(s, true, in_combat, true));        // moving -> shown

    s.out_of_combat_mode = cursor_finder::OutOfCombatMode::Never;
    CHECK_FALSE(cursor_finder::should_show_marker(s, true, in_combat, false));
    CHECK_FALSE(cursor_finder::should_show_marker(s, true, in_combat, true));
}

TEST_CASE("should_show_marker: in-combat column Always/Never, motion ignored")
{
    cursor_finder::CursorSettings s;
    s.enabled            = true;
    const bool in_combat = true;

    s.in_combat_mode = cursor_finder::InCombatMode::Always;
    CHECK(cursor_finder::should_show_marker(s, true, in_combat, false));
    CHECK(cursor_finder::should_show_marker(s, true, in_combat, true)); // motion irrelevant

    s.in_combat_mode = cursor_finder::InCombatMode::Never;
    CHECK_FALSE(cursor_finder::should_show_marker(s, true, in_combat, false));
    CHECK_FALSE(cursor_finder::should_show_marker(s, true, in_combat, true));
}

// --- schema v6 persistence + migration ---------------------------------------

TEST_CASE("every visibility slug round-trips through the enums")
{
    for (const cursor_finder::OutOfCombatMode m : {cursor_finder::OutOfCombatMode::Always,
                                            cursor_finder::OutOfCombatMode::WhileMoving,
                                            cursor_finder::OutOfCombatMode::Never})
    {
        const auto back = cursor_finder::out_of_combat_from_slug(cursor_finder::out_of_combat_to_slug(m));
        REQUIRE(back.has_value());
        CHECK(*back == m);
    }
    for (const cursor_finder::InCombatMode m : {cursor_finder::InCombatMode::Always,
                                         cursor_finder::InCombatMode::Never})
    {
        const auto back = cursor_finder::in_combat_from_slug(cursor_finder::in_combat_to_slug(m));
        REQUIRE(back.has_value());
        CHECK(*back == m);
    }
    // "while_moving" is a valid OUT-of-combat value but NOT a valid in-combat one.
    CHECK(cursor_finder::out_of_combat_from_slug("while_moving").has_value());
    CHECK_FALSE(cursor_finder::in_combat_from_slug("while_moving").has_value());
}

TEST_CASE("a v5 file (no visibility modes) migrates forward to v6 with the defaults")
{
    TempStorePath tmp;
    {
        // Exactly what a v5 build wrote: schema 5, every field up to the QA icon, but
        // no visibility modes.
        std::ofstream out(tmp.path);
        out << R"({"schema_version":5,"enabled":true,"draw_above_windows":true,)"
               R"("preset":"pulse_ring","colour":"#ff2d9b","size_px":96,)"
               R"("opacity_pct":90,"outline":true,"outline_colour":"#141418",)"
               R"("fill":false,"fill_opacity_pct":35,"fill_colour":"#f2f2f6",)"
               R"("fill_size_pct":70,"freeze_after_drag":false,)"
               R"("show_quick_access_icon":true})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().out_of_combat_mode == cursor_finder::OutOfCombatMode::Always); // absent -> default
    CHECK(store.settings().in_combat_mode == cursor_finder::InCombatMode::Always);        // absent -> default

    // Re-stamped at v6 with the modes present; a change survives a reload.
    cursor_finder::CursorSettings next = store.settings();
    next.out_of_combat_mode = cursor_finder::OutOfCombatMode::WhileMoving;
    next.in_combat_mode     = cursor_finder::InCombatMode::Never;
    CHECK(store.set(next));
    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["schema_version"] == 7);
    CHECK(on_disk["out_of_combat_mode"] == "while_moving");
    CHECK(on_disk["in_combat_mode"] == "never");

    cursor_finder::CursorStore reloaded(tmp.path);
    CHECK(reloaded.settings().out_of_combat_mode == cursor_finder::OutOfCombatMode::WhileMoving);
    CHECK(reloaded.settings().in_combat_mode == cursor_finder::InCombatMode::Never);
}

TEST_CASE("an invalid in-combat 'while_moving' slug coerces to the default")
{
    TempStorePath tmp;
    {
        // while_moving is valid out-of-combat but must not be a valid in-combat value.
        std::ofstream out(tmp.path);
        out << R"({"schema_version":6,"in_combat_mode":"while_moving",)"
               R"("out_of_combat_mode":"never"})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().in_combat_mode == cursor_finder::InCombatMode::Always);        // coerced to default
    CHECK(store.settings().out_of_combat_mode == cursor_finder::OutOfCombatMode::Never);  // valid, honoured
}

TEST_CASE("an unknown visibility slug degrades to its default")
{
    TempStorePath tmp;
    {
        std::ofstream out(tmp.path);
        out << R"({"schema_version":6,"out_of_combat_mode":"sometimes",)"
               R"("in_combat_mode":"maybe"})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().out_of_combat_mode == cursor_finder::OutOfCombatMode::Always);
    CHECK(store.settings().in_combat_mode == cursor_finder::InCombatMode::Always);
}

// --- schema v7 — the first-run `welcomed` flag -------------------------------
//
// The one-time first-run auto-open. A brand-new install has NO file, so
// load() returns defaults() with welcomed == false -> should_first_run_open is true
// -> the DLL glue opens the designed window once and persists welcomed = true. An
// EXISTING install (any on-disk file predating this key) migrates forward as
// already-welcomed, so upgrading users get no surprise auto-open. The
// window-open + immediate persist is the Windows/in-game surface (AddonLoad glue).

TEST_CASE("a fresh default record is not yet welcomed; should_first_run_open is true")
{
    const cursor_finder::CursorSettings s = cursor_finder::CursorSettings::defaults();
    CHECK_FALSE(s.welcomed);                    // brand-new install: not welcomed
    CHECK(cursor_finder::should_first_run_open(s));    // ...so the first-run open fires
}

TEST_CASE("should_first_run_open is false once welcomed")
{
    cursor_finder::CursorSettings s = cursor_finder::CursorSettings::defaults();
    s.welcomed = true;
    CHECK_FALSE(cursor_finder::should_first_run_open(s)); // never re-opens on later launches
}

TEST_CASE("a missing file (fresh install) loads not-welcomed so the first-run open fires")
{
    TempStorePath tmp;
    REQUIRE_FALSE(fs::exists(tmp.path));
    cursor_finder::CursorStore store(tmp.path);
    CHECK_FALSE(store.settings().welcomed);          // only a brand-new install auto-opens
    CHECK(cursor_finder::should_first_run_open(store.settings()));
}

TEST_CASE("an existing v6 file (no welcomed key) migrates forward as ALREADY welcomed")
{
    TempStorePath tmp;
    {
        // Exactly what a v6 build wrote: schema 6, the visibility modes, but no welcomed.
        std::ofstream out(tmp.path);
        out << R"({"schema_version":6,"enabled":true,"draw_above_windows":true,)"
               R"("preset":"pulse_ring","colour":"#ff2d9b","size_px":96,)"
               R"("opacity_pct":90,"outline":true,"outline_colour":"#141418",)"
               R"("fill":false,"fill_opacity_pct":35,"fill_colour":"#f2f2f6",)"
               R"("fill_size_pct":70,"freeze_after_drag":false,)"
               R"("show_quick_access_icon":true,"out_of_combat_mode":"always",)"
               R"("in_combat_mode":"always"})";
    }
    cursor_finder::CursorStore store(tmp.path);
    CHECK(store.settings().welcomed);                          // upgrade: no surprise auto-open
    CHECK_FALSE(cursor_finder::should_first_run_open(store.settings()));

    // Re-stamped at v7 with the welcomed key present on the next write-through.
    cursor_finder::CursorSettings next = store.settings();
    next.enabled = false;
    CHECK(store.set(next));
    json on_disk = json::parse(read_disk(tmp.path));
    CHECK(on_disk["schema_version"] == 7);
    CHECK(on_disk["welcomed"] == true);
}

TEST_CASE("the welcomed flag round-trips through disk at v7")
{
    TempStorePath tmp;
    {
        cursor_finder::CursorStore store(tmp.path);
        cursor_finder::CursorSettings next = store.settings();
        next.welcomed = true;               // the first-run open just fired + persisted it
        CHECK(store.set(next));
        json on_disk = json::parse(read_disk(tmp.path));
        CHECK(on_disk["welcomed"] == true);
    }
    cursor_finder::CursorStore reloaded(tmp.path);
    CHECK(reloaded.settings().welcomed);     // survives a restart -> never re-opens
    CHECK_FALSE(cursor_finder::should_first_run_open(reloaded.settings()));
}

// The Reset-to-defaults edge: Reset restores visual defaults but must NOT
// clear welcomed, or the auto-open re-triggers next launch. The preserve-welcomed
// rule lives inline in entry.cpp's Reset button glue (capture w, reset, restore w);
// this asserts the pure logic pattern that glue implements.
TEST_CASE("reset-to-defaults preserves welcomed, resets visuals")
{
    cursor_finder::CursorSettings s = cursor_finder::CursorSettings::defaults();
    s.welcomed = true;                    // already welcomed
    s.preset   = cursor_finder::Preset::SoftHalo; // ...with customised visuals
    s.size_px  = 100;

    // The Reset glue: capture welcomed, reset to visual defaults, restore welcomed.
    const bool w = s.welcomed;
    s = cursor_finder::CursorSettings::defaults();
    s.welcomed = w;

    CHECK(s.preset == cursor_finder::Preset::PulseRing); // visuals reset
    CHECK(s.size_px == 96);                       // visuals reset
    CHECK(s.welcomed);                            // welcomed preserved -> no re-open
    CHECK_FALSE(cursor_finder::should_first_run_open(s));
}

// The gold L-shaped corner ornaments are removed from the shared theme
// everywhere. DrawThemedFrame's
// drawing is ImGui-only (outside this harness), so this guards the data that
// drove it: the palette no longer carries a `corner` ornament entry. Detection
// idiom, evaluated at compile time and checked at run time so the suite goes
// red (not a build break) while the member exists.
namespace detail {
template <class T, class = void>
struct has_corner_ornament : std::false_type {};
template <class T>
struct has_corner_ornament<T, std::void_t<decltype(std::declval<T&>().corner)>>
    : std::true_type {};
} // namespace detail

TEST_CASE("theme: no corner ornament in the shared palette")
{
    CHECK_FALSE(detail::has_corner_ornament<shared::theme::Palette>::value);
}

// --- settings location: one-time move from the legacy folder ----------------
//
// Settings live at <addons>/cursor-finder/cursor-finder.json. Installs from
// before the rename have <addons>/cursor/cursor.json; resolve_settings_path()
// moves it once and returns the path the store should use. If the move cannot
// happen it returns the legacy path, so the real settings still load (and the
// first-run window never writes defaults over them at the new path).

namespace {

// A unique temp "addons" folder per test, removed on destruction.
struct TempAddonsRoot {
    fs::path root;
    TempAddonsRoot()
    {
        static int counter = 0;
        root = fs::temp_directory_path() /
               ("cursor-finder-core-addons-" + std::to_string(counter++));
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
    }
    ~TempAddonsRoot()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void write_file(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << text;
}

} // namespace

TEST_CASE("settings paths are built from the addons root")
{
    const fs::path root = fs::path("addons");
    CHECK(cursor_finder::settings_file(root) == root / "cursor-finder" / "cursor-finder.json");
    CHECK(cursor_finder::legacy_settings_file(root) == root / "cursor" / "cursor.json");
}

TEST_CASE("resolve_settings_path: no file anywhere -> the new path, nothing created")
{
    TempAddonsRoot t;
    const fs::path next   = cursor_finder::settings_file(t.root);
    const fs::path legacy = cursor_finder::legacy_settings_file(t.root);
    CHECK(cursor_finder::resolve_settings_path(next, legacy) == next);
    CHECK_FALSE(fs::exists(next));
    CHECK_FALSE(fs::exists(legacy));
}

TEST_CASE("resolve_settings_path: only the new file -> the new path")
{
    TempAddonsRoot t;
    const fs::path next   = cursor_finder::settings_file(t.root);
    const fs::path legacy = cursor_finder::legacy_settings_file(t.root);
    write_file(next, "{\"new\":true}");
    CHECK(cursor_finder::resolve_settings_path(next, legacy) == next);
    CHECK(read_disk(next) == "{\"new\":true}");
}

TEST_CASE("resolve_settings_path: only the legacy file -> moved, old folder removed")
{
    TempAddonsRoot t;
    const fs::path next   = cursor_finder::settings_file(t.root);
    const fs::path legacy = cursor_finder::legacy_settings_file(t.root);
    write_file(legacy, "{\"legacy\":true}");
    CHECK(cursor_finder::resolve_settings_path(next, legacy) == next);
    CHECK(read_disk(next) == "{\"legacy\":true}");
    CHECK_FALSE(fs::exists(legacy));
    CHECK_FALSE(fs::exists(legacy.parent_path()));
}

TEST_CASE("resolve_settings_path: a stray legacy .tmp does not keep the old folder")
{
    TempAddonsRoot t;
    const fs::path next   = cursor_finder::settings_file(t.root);
    const fs::path legacy = cursor_finder::legacy_settings_file(t.root);
    write_file(legacy, "{\"legacy\":true}");
    write_file(fs::path(legacy.string() + ".tmp"), "partial");
    CHECK(cursor_finder::resolve_settings_path(next, legacy) == next);
    CHECK(read_disk(next) == "{\"legacy\":true}");
    CHECK_FALSE(fs::exists(legacy.parent_path()));
}

TEST_CASE("resolve_settings_path: both files -> the new one wins, legacy untouched")
{
    TempAddonsRoot t;
    const fs::path next   = cursor_finder::settings_file(t.root);
    const fs::path legacy = cursor_finder::legacy_settings_file(t.root);
    write_file(next, "{\"new\":true}");
    write_file(legacy, "{\"legacy\":true}");
    CHECK(cursor_finder::resolve_settings_path(next, legacy) == next);
    CHECK(read_disk(next) == "{\"new\":true}");
    CHECK(read_disk(legacy) == "{\"legacy\":true}");
}

TEST_CASE("resolve_settings_path: a move that cannot happen -> the legacy path, file intact")
{
    TempAddonsRoot t;
    const fs::path next   = cursor_finder::settings_file(t.root);
    const fs::path legacy = cursor_finder::legacy_settings_file(t.root);
    write_file(legacy, "{\"legacy\":true}");
    // Occupy the new folder's name with a plain file, so neither a move nor a
    // copy into it is possible.
    write_file(next.parent_path(), "not a folder");
    CHECK(cursor_finder::resolve_settings_path(next, legacy) == legacy);
    CHECK(read_disk(legacy) == "{\"legacy\":true}");
}

TEST_CASE("a failed move still loads the real settings and never re-runs first-run")
{
    TempAddonsRoot t;
    const fs::path next   = cursor_finder::settings_file(t.root);
    const fs::path legacy = cursor_finder::legacy_settings_file(t.root);
    {
        cursor_finder::CursorStore old_store(legacy);
        cursor_finder::CursorSettings s = old_store.settings();
        s.welcomed           = true;
        s.out_of_combat_mode = cursor_finder::OutOfCombatMode::WhileMoving;
        REQUIRE(old_store.set(s));
    }
    write_file(next.parent_path(), "not a folder"); // force the move to fail

    // The same sequence AddonLoad runs: resolve, load, first-run decision.
    const fs::path use = cursor_finder::resolve_settings_path(next, legacy);
    cursor_finder::CursorStore store(use);
    CHECK(store.settings().out_of_combat_mode == cursor_finder::OutOfCombatMode::WhileMoving);
    CHECK_FALSE(cursor_finder::should_first_run_open(store.settings()));
    CHECK_FALSE(fs::is_regular_file(next));
}

TEST_CASE("a root with a trailing separator still finds and moves the legacy file")
{
    TempAddonsRoot t;
    const fs::path root   = fs::path(t.root.string() + "/");
    const fs::path next   = cursor_finder::settings_file(root);
    const fs::path legacy = cursor_finder::legacy_settings_file(root);
    write_file(t.root / "cursor" / "cursor.json", "{\"legacy\":true}");
    CHECK(cursor_finder::resolve_settings_path(next, legacy) == next);
    CHECK(read_disk(t.root / "cursor-finder" / "cursor-finder.json") == "{\"legacy\":true}");
    CHECK_FALSE(fs::exists(t.root / "cursor"));
}
