// The HUD a level is played under and the seconds it opens with, as numbers.
//
// sim/Hud.hpp turns the port's ui.json into rectangles and alphas, and this pins
// both halves: what the file says, and what the arithmetic makes of it. Every
// expectation below is stated in the units it was MEASURED in where it was
// measured - screen pixels of a 1280x720 capture of the original, at 2.8125 px a
// design unit - so a number here can be held against static_hud.md or
// level_start_timeline.md without converting anything back.
//
// Pure: no window, no registry, no level. ui.json is the port's own and
// committed, so this runs anywhere; the caption's layout is tested on a font it
// writes, never on the original's.

#include "TestHarness.hpp"

#include "core/BitmapFont.hpp"

#include "sim/Hud.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using namespace MagicPortals;

namespace {

const std::string kUi = std::string(MAGICPORTALS_PORT_DATA_DIR) + "/ui.json";

// The original's captures: 1280x720, and 2.8125 screen px to a design unit.
constexpr double kPxPerUnit = 2.8125;
const glm::dvec2 kView720(1280.0 / kPxPerUnit, 720.0 / kPxPerUnit); // 455.11 x 256

bool Near(double a, double b, double eps) {
    return std::fabs(a - b) <= eps;
}

std::string Num(double v) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f", v);
    return buffer;
}

Hud::Rules Load() {
    Hud::Rules rules;
    std::string error;
    CHECK_MSG(Hud::LoadRules(kUi, rules, error), "ui.json reads: " + error);
    return rules;
}

std::filesystem::path Scratch() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "supersonic-test-mp-hud";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string Write(const char* name, const std::string& text) {
    const std::filesystem::path path = Scratch() / name;
    std::ofstream file(path, std::ios::trunc);
    file << text;
    return path.string();
}

// A rectangle in the capture's pixels, to be compared with what was measured.
bool RectPx(const Hud::Rect& rect, double x, double y, double w, double h, double eps = 0.5) {
    return Near(rect.min.x * kPxPerUnit, x, eps) && Near(rect.min.y * kPxPerUnit, y, eps) &&
           Near(rect.size.x * kPxPerUnit, w, eps) && Near(rect.size.y * kPxPerUnit, h, eps);
}

std::string ShowPx(const Hud::Rect& rect) {
    return "(" + Num(rect.min.x * kPxPerUnit) + ", " + Num(rect.min.y * kPxPerUnit) + ") " +
           Num(rect.size.x * kPxPerUnit) + " x " + Num(rect.size.y * kPxPerUnit) + " px";
}

void TheFileSaysWhatWasMeasuredAndDecoded() {
    const Hud::Rules rules = Load();
    // 0x78FFFFFF, the one colour every control is drawn in.
    CHECK_EQ(rules.alphaByte, 120);
    CHECK_MSG(Near(Hud::Opacity(rules), 0.48, 0.02), "0.4706 is inside the measured 0.48 +-0.02");

    CHECK(rules.restart.sprite == "restart_level_button.png");
    CHECK(rules.restart.anchor == Hud::Anchor::TopRight);
    CHECK(rules.pause.sprite == "main_menu_shortcut.png");
    CHECK(rules.pause.anchor == Hud::Anchor::TopRight);
    CHECK(rules.clearPortals.sprite == "clear_portals_button.png");
    CHECK(rules.clearPortals.anchor == Hud::Anchor::TopLeft);

    CHECK(rules.pads.leftSprite == "arrow_left.png");
    CHECK(rules.pads.rightSprite == "arrow_right.png");
    CHECK_EQ(rules.pads.sizeUnits, 64.0);
    CHECK_EQ(rules.pads.hitRadiusUnits, 64.0);
    CHECK_EQ(rules.pads.slideFromUnits, 128.0);
    CHECK_EQ(rules.pads.slideMs, 700.0);
    CHECK_EQ(rules.pads.strideMs, 300.0);
    CHECK_EQ(rules.pads.strides, 14);
    CHECK_EQ(rules.pads.variationByte, 40);
    CHECK(rules.pads.tutorialLevel == "level0");
    CHECK_EQ(rules.pads.tutorialStrides, 48);
    CHECK_EQ(rules.pads.tutorialVariationByte, 90);
    CHECK(rules.pads.ringSprite == "ring_sprite.png");
    CHECK_EQ(rules.pads.ringStrideMs, 1200.0);
    CHECK_EQ(rules.pads.ringSizeUnits, 200.0);

    CHECK_EQ(rules.overlay.fadeMs, 700.0);
    CHECK_EQ(rules.overlay.layers, 2);
    CHECK_EQ(rules.overlay.layersOverPads, 1);
    CHECK_EQ(rules.overlay.startAfterMs, 465.0);

    CHECK(rules.caption.font == "Matura84_shadow.fnt");
    CHECK(rules.caption.prefix == "Part ");
    CHECK_EQ(rules.caption.unitsPerFontPx, 0.5);
    CHECK(rules.caption.centreOfView == glm::dvec2(0.5, 0.8));
    CHECK_EQ(rules.caption.fadeMs, 3000.0);

    CHECK(rules.plaque.sprite == "current_score_plaque.png");
    CHECK(rules.plaque.medalGold == "medal_gold_l.png");
    CHECK_EQ(rules.plaque.appearMs, 1000.0);
    CHECK_EQ(rules.plaque.dismissAfterMs, 2000.0);
    CHECK_EQ(rules.plaque.dismissMs, 1000.0);
}

void TheButtonsSitWhereTheCapturesPutThem() {
    const Hud::Rules rules = Load();
    // static_hud.md section 3, 123 of 123 frames each.
    const Hud::Rect restart = Hud::Place(rules.restart, kView720);
    const Hud::Rect pause = Hud::Place(rules.pause, kView720);
    CHECK_MSG(RectPx(restart, 1100.0, 0.0, 90.0, 90.0), "restart at (1100, 0) 90 px: " + ShowPx(restart));
    CHECK_MSG(RectPx(pause, 1190.0, 0.0, 90.0, 90.0), "pause at (1190, 0) 90 px: " + ShowPx(pause));
    CHECK_MSG(Near(restart.Max().x, pause.min.x, 1e-9), "the two touch, with no gap");
    CHECK_MSG(Near(pause.Max().x, kView720.x, 1e-9), "and the pause button is flush with the right edge");

    // Anchored to the corner, so a window of another shape moves them with it.
    const glm::dvec2 view43(256.0 * 4.0 / 3.0, 256.0);
    CHECK_MSG(Near(Hud::Place(rules.pause, view43).Max().x, view43.x, 1e-9) &&
                  Near(Hud::Place(rules.restart, view43).min.x, view43.x - 64.0, 1e-9),
              "at 4:3 the pair still hugs the right edge");

    const Hud::Rect clear = Hud::Place(rules.clearPortals, kView720);
    CHECK_MSG(RectPx(clear, 0.0, 0.0, 90.0, 90.0), "clear-portals at (0, 0) 90 px: " + ShowPx(clear));
}

void ThePadsSlideInAndSitInTheCorners() {
    const Hud::Rules rules = Load();
    // Settled: static_hud.md's padfit, 39 of 41 levels exactly.
    const Hud::Rect left = Hud::PadRect(rules, Hud::Side::Left, kView720, 8000.0);
    const Hud::Rect right = Hud::PadRect(rules, Hud::Side::Right, kView720, 8000.0);
    CHECK_MSG(RectPx(left, 0.0, 540.0, 180.0, 180.0), "left pad at (0, 540) 180 px: " + ShowPx(left));
    CHECK_MSG(RectPx(right, 1100.0, 540.0, 180.0, 180.0), "right pad at (1100, 540) 180 px: " + ShowPx(right));

    // ScreenPad's interpolators: 128 units outside the corner at zero, in by 700 ms.
    CHECK_EQ(Hud::PadSlideUnits(rules, 0.0), 128.0);
    CHECK_EQ(Hud::PadSlideUnits(rules, 700.0), 0.0);
    CHECK_MSG(Hud::PadRect(rules, Hud::Side::Left, kView720, 0.0).Max().x <= 0.0 &&
                  Hud::PadRect(rules, Hud::Side::Right, kView720, 0.0).min.x >= kView720.x,
              "at the first tick both pads are wholly off the screen");
    // smoothEnd reaches a half at a third of the way: the pad's own width in.
    CHECK_MSG(Near(Hud::PadSlideUnits(rules, 700.0 / 3.0), 64.0, 1e-9),
              "a third of the way through, a pad is its own width out: " +
                  Num(Hud::PadSlideUnits(rules, 700.0 / 3.0)));
    bool monotonic = true;
    for (double t = 0.0; t < 700.0; t += 16.0) {
        if (Hud::PadSlideUnits(rules, t + 16.0) > Hud::PadSlideUnits(rules, t)) monotonic = false;
    }
    CHECK_MSG(monotonic, "and it only ever comes in");

    // The hit circle is about the pad's corner, 64 units: the drawn disc and more.
    const glm::dvec2 corner(0.0, kView720.y);
    CHECK(Hud::OnPad(rules, Hud::Side::Left, kView720, 8000.0, corner + glm::dvec2(30.0, -30.0)));
    CHECK(Hud::OnPad(rules, Hud::Side::Left, kView720, 8000.0, corner + glm::dvec2(0.0, -63.9)));
    CHECK_MSG(!Hud::OnPad(rules, Hud::Side::Left, kView720, 8000.0, corner + glm::dvec2(45.3, -45.3)),
              "the square's far corner is outside the circle");
    CHECK(!Hud::OnPad(rules, Hud::Side::Right, kView720, 8000.0, corner + glm::dvec2(30.0, -30.0)));
    CHECK(Hud::OnPad(rules, Hud::Side::Right, kView720, 8000.0, glm::dvec2(kView720.x - 1.0, kView720.y - 1.0)));
}

void ThePadsPulseAsComputeButtonColorSays() {
    const Hud::Rules rules = Load();
    const auto byteAt = [&rules](double ms, bool tutorial) {
        return static_cast<int>(std::lround(Hud::PadOpacity(rules, ms, tutorial) * 255.0));
    };
    // A triangle two strides long, from the flat 120 up to 120 + variation.
    CHECK_EQ(byteAt(0.0, false), 120);
    CHECK_EQ(byteAt(150.0, false), 140);
    CHECK_EQ(byteAt(299.0, false), 159);
    CHECK_EQ(byteAt(300.0, false), 160);
    CHECK_EQ(byteAt(450.0, false), 140);
    CHECK_EQ(byteAt(600.0, false), 120);
    CHECK_EQ(byteAt(4199.0, false), 120 + static_cast<int>((1.0 - 299.0 / 300.0) * 40.0));
    CHECK_MSG(byteAt(4200.0, false) == 120 && byteAt(8000.0, false) == 120,
              "every level's pulse stops after fourteen strides, 4.2 s");

    // The tutorial's: 120 <-> 210 of 255 at 600 ms, which static_hud.md 5a fitted
    // as 0.475..0.496 <-> 0.805..0.824, triangle over sine, period 0.609 s.
    CHECK_EQ(byteAt(300.0, true), 210);
    CHECK_EQ(byteAt(900.0, true), 210);
    CHECK_EQ(byteAt(1200.0, true), 120);
    CHECK_MSG(Near(Hud::PadOpacity(rules, 300.0, true), 0.815, 0.011), "the peak is inside the measured peaks");
    CHECK_MSG(byteAt(14399.0, true) != 120 || byteAt(14100.0, true) > 120, "still pulsing just before 14.4 s");
    CHECK_EQ(byteAt(14400.0, true), 120);
}

void TheTutorialRingGrowsAndFades() {
    const Hud::Rules rules = Load();
    CHECK_MSG(!Hud::RingAt(rules, 600.0, false).shown, "no ring outside the tutorial");
    const Hud::Ring start = Hud::RingAt(rules, 0.0, true);
    CHECK(start.shown);
    CHECK_EQ(start.sizeUnits, 2.0); // max(bias, 0.01) of 200
    CHECK_EQ(start.alpha, 1.0);
    const Hud::Ring half = Hud::RingAt(rules, 600.0, true);
    CHECK_EQ(half.sizeUnits, 100.0);
    CHECK_EQ(half.alpha, 127.0 / 255.0);
    CHECK_MSG(Near(Hud::RingAt(rules, 1199.0, true).sizeUnits, 199.83, 0.01), "near 200 units at the stride's end");
    CHECK_EQ(Hud::RingAt(rules, 1200.0, true).sizeUnits, 2.0);
    CHECK_MSG(!Hud::RingAt(rules, 14400.0, true).shown, "and it stops with the pulse");
    // A new ring every 1.2 s starts where the base pulse is at its trough, every
    // second one - static_hud.md 5b's "every second trough".
    for (int k = 0; k < 11; ++k) {
        const double ms = 1200.0 * k;
        CHECK_EQ(static_cast<int>(std::lround(Hud::PadOpacity(rules, ms, true) * 255.0)), 120);
    }
}

void TheLevelOpensInTheDark() {
    const Hud::Rules rules = Load();
    const double start = rules.overlay.startAfterMs;
    // Wholly black from the level's first tick until the blacks' own clock
    // starts - the original's GetTime() is read only after its load - then two
    // linear blacks of 700 ms.
    CHECK_EQ(Hud::OverlayAlpha(rules, 0.0), 1.0);
    CHECK_EQ(Hud::OverlayAlpha(rules, start * 0.5), 1.0);
    CHECK_EQ(Hud::OverlayAlpha(rules, start), 1.0);
    CHECK_MSG(Hud::OverlayAlpha(rules, start + 17.0) < 1.0, "and lifting the tick after it starts");
    CHECK_MSG(Hud::OverlayAlpha(rules, start + 690.0) > 0.0, "still a 255th of black 10 ms before its end");
    CHECK_EQ(Hud::OverlayAlpha(rules, start + 700.0), 0.0);
    CHECK_EQ(Hud::OverlayAlpha(rules, 5000.0), 0.0);
    // Stacked, what shows through is ((t - start) / 0.7)^2, to a 255th of each layer.
    for (double ms = 50.0; ms < 700.0; ms += 50.0) {
        const double through = 1.0 - Hud::OverlayAlpha(rules, start + ms);
        const double squared = (ms / 700.0) * (ms / 700.0);
        CHECK_MSG(Near(through, squared, 0.01), "at the black's " + Num(ms) + " ms the picture shows " +
                                                   Num(through) + ", (t/0.7)^2 is " + Num(squared));
    }
}

// rec11, the only footage of an opening, frame by frame on ONE clock: each lit
// frame is dated by its own caption (alpha = 1 - t / 3000), and at that age the
// port must show the world as bright as the capture did (level_start_timeline.md
// sections 1-2), the pads already home and the plaque already up. Dated by the
// caption, a black that started with the level would be gone by n29; this is
// the test that tells the two apart.
void TheOpeningIsRec11sFrameByFrame() {
    const Hud::Rules rules = Load();
    const struct {
        int n;
        double caption; // interior median
        double k;       // world brightness under both blacks
    } frames[] = {{28, 0.765, 0.094}, {29, 0.723, 0.262}, {30, 0.714, 0.335}, {31, 0.696, 0.400}, {32, 0.686, 0.462},
                  {33, 0.673, 0.525}, {34, 0.667, 0.584}, {35, 0.657, 0.658}, {36, 0.648, 0.753}};
    for (const auto& frame : frames) {
        const double ageMs = 3000.0 * (1.0 - frame.caption);
        const double through = 1.0 - Hud::OverlayAlpha(rules, ageMs);
        const std::string at = "n" + std::to_string(frame.n) + " (caption " + Num(frame.caption) + ", " +
                               Num(ageMs) + " ms into the level)";
        // h264 and frames presented late are why this is not tight; a black on
        // the level's own clock misses these by 0.25..0.9.
        CHECK_MSG(Near(through, frame.k, 0.05), at + ": world measured " + Num(frame.k) + ", port " + Num(through));
        CHECK_MSG(Hud::PadSlideUnits(rules, ageMs) < 0.1, at + ": the pads are home, as every lit frame has them");
        CHECK_MSG(Hud::PlaqueAlpha(rules, ageMs) > 0.89, at + ": the plaque is up, " + Num(Hud::PlaqueAlpha(rules, ageMs)));
    }
    // n27, the black frame, is 24 ms into the level by its caption (0.992): whole black.
    CHECK_EQ(Hud::OverlayAlpha(rules, 24.0), 1.0);
}

void ThePadsSitBetweenTheTwoBlacks() {
    const Hud::Rules rules = Load();
    // What the pads are covered by is one layer: brighter than the world under
    // both, which is what level_start_timeline measured and could not explain.
    for (double ms = 100.0; ms < 700.0; ms += 100.0) {
        const double age = rules.overlay.startAfterMs + ms;
        const double onPads = 1.0 - Hud::OverlayLayersAlpha(rules, age, rules.overlay.layersOverPads);
        const double onWorld = 1.0 - Hud::OverlayAlpha(rules, age);
        CHECK_MSG(Near(onWorld, onPads * onPads, 1e-9) && onPads > onWorld,
                  "at " + Num(ms) + " ms the pads show " + Num(onPads) + " and the world " + Num(onWorld));
    }
    // And during the hold the port paints the one over the pads whole as well.
    // INFERRED, not decoded: in the original neither black exists yet then, and
    // no frame is drawn - the port draws its held frames as rec11's black frame
    // n27 looks, the one frame of the load that was drawn.
    CHECK_EQ(Hud::OverlayLayersAlpha(rules, rules.overlay.startAfterMs * 0.5, rules.overlay.layersOverPads), 1.0);
    CHECK_EQ(Hud::OverlayLayersAlpha(rules, 350.0, 0), 0.0);
}

void TheNoPortalSignFollowsTheCameraCorner() {
    const Hud::Rules rules = Load();
    CHECK(rules.sign.entity == "no_portal_sign.ent");
    CHECK_EQ(rules.sign.followMs, 600.0);
    CHECK_EQ(rules.sign.retargetMs, 100.0);
    CHECK_EQ(rules.sign.centreInBySize, 0.5);

    // Settled on a still camera, its 64-unit picture sits flush in the corner:
    // screen (0, 0), 180 px, as 1-15 and 2-07 show it.
    const glm::dvec2 size(64.0);
    const glm::dvec2 corner(120.0, 40.0); // the camera's top-left in the level
    const glm::dvec2 target = Hud::SignTarget(rules, corner, size);
    const Hud::Rect settled{target - size * 0.5 - corner, size};
    CHECK_MSG(RectPx(settled, 0.0, 0.0, 180.0, 180.0), "settled at " + ShowPx(settled));

    // From where the level placed the entity, (-61, -24): a smoothEnd ease of
    // 600 ms on frame time, AIMED AGAIN from wherever it has got to each time
    // more than 100 ms have passed while it is not yet there. So it never runs
    // a whole ease: it closes about 30 % of the way every 117 ms and settles
    // geometrically - within a pixel of the corner by 2 s, which is where the
    // library's t2.0 frames of 1-15 already find it.
    Hud::Follow follow;
    const glm::dvec2 placed(-61.0, -24.0);
    follow.at = placed;
    follow.Tick(rules, target, 0.0);
    CHECK_MSG(follow.at == placed, "the first frame of no time leaves it where it stands");
    const double tick = 1000.0 / 60.0;
    int ticks = 0;
    for (; ticks < 5; ++ticks) follow.Tick(rules, target, tick); // 83 ms: still the first ease
    const glm::dvec2 first = placed + (target - placed) * Hud::SmoothEnd(5.0 * tick / 600.0);
    CHECK_MSG(glm::length(follow.at - first) < 1e-9 && follow.from == placed,
              "for its first 100 ms it is on its first ease: " + Num(follow.at.x) + ", " + Num(follow.at.y));
    for (; ticks < 7; ++ticks) follow.Tick(rules, target, tick); // 117 ms
    CHECK_MSG(follow.from != placed && follow.to == target, "past 100 ms it is aimed again, from where it is");
    for (; ticks < 120; ++ticks) follow.Tick(rules, target, tick); // 2 s
    const double atTwoS = glm::length(follow.at - target) / glm::length(target - placed);
    CHECK_MSG(atTwoS < 0.003 && atTwoS > 0.0,
              "a quarter of a percent of the way left at 2 s, and still closing: " + Num(atTwoS));
    for (; ticks < 1200; ++ticks) follow.Tick(rules, target, tick); // 20 s
    CHECK_MSG(glm::length(follow.at - target) < 1e-9, "and settled on it");

    // Standing exactly on its target it is never aimed again.
    Hud::Follow still;
    still.at = target;
    still.Tick(rules, target, 0.0);
    still.Tick(rules, target, 150.0);
    CHECK_MSG(still.at == target && still.from == target && Near(still.elapsedMs, 150.0, 1e-9),
              "a sign already in the corner stays put");

    // The camera moves: the sign is aimed again only once more than 100 ms have
    // passed since it was last aimed, and then eases from where it is.
    Hud::Follow lag;
    lag.at = target;
    lag.Tick(rules, target, 0.0);
    lag.Tick(rules, target, 150.0); // settled, and 150 ms since aimed
    const glm::dvec2 moved = target + glm::dvec2(30.0, 0.0);
    lag.Tick(rules, moved, tick);
    CHECK_MSG(lag.to == moved && lag.from == target && Near(lag.elapsedMs, tick, 1e-9),
              "a moved camera re-aims a sign that has waited 150 ms");
    const glm::dvec2 movedAgain = moved + glm::dvec2(30.0, 0.0);
    for (int i = 0; i < 5; ++i) lag.Tick(rules, movedAgain, tick); // 83 ms more
    CHECK_MSG(lag.to == moved, "but not again within 100 ms of that");
    for (int i = 0; i < 2; ++i) lag.Tick(rules, movedAgain, tick); // past 100 ms
    CHECK_MSG(lag.to == movedAgain, "and again once they have passed");
    CHECK_MSG(lag.at.x < movedAgain.x, "trailing the corner as it goes: " + Num(lag.at.x));
}

// The sign's chase is the one clock of the opening that depends on how its time
// is cut into frames, so it is handed the black's hold in one piece, as the
// original's load reaches it in one frame's delta.
void TheSignIsHandedTheLoadInOnePiece() {
    const Hud::Rules rules = Load();
    const double tick = 1000.0 / 60.0;
    double held = 0.0;
    double handed = 0.0;
    int firstHanded = -1;
    for (int n = 1; n <= 40; ++n) {
        const double got = Hud::HandOver(rules, n * tick, tick, held);
        if (got > 0.0 && firstHanded < 0) {
            firstHanded = n;
            handed = got;
        } else if (firstHanded > 0) {
            CHECK_MSG(Near(got, tick, 1e-9), "a tick's own time after it: " + Num(got));
        } else {
            CHECK_MSG(got == 0.0, "nothing while the blacks are whole, tick " + std::to_string(n));
        }
    }
    // 28 ticks = 466.7 ms is the first past 465.
    CHECK_EQ(firstHanded, 28);
    CHECK_MSG(Near(handed, 28.0 * tick, 1e-9), "all 28 ticks at once: " + Num(handed));

    // 1-15's placement, a still camera at (0, 0): at 700 ms - rec11's first lit
    // frame, the world at 0.11 - a chase fed sixty short frames is still 13 units
    // out; fed the hold in one piece it is 3.
    const glm::dvec2 size(64.0);
    const glm::dvec2 target = Hud::SignTarget(rules, glm::dvec2(0.0), size);
    const glm::dvec2 placed(-61.0, -24.0);
    Hud::Follow chase;
    Hud::Follow loaded;
    chase.at = loaded.at = placed;
    chase.Tick(rules, target, 0.0);
    loaded.Tick(rules, target, 0.0);
    double owed = 0.0;
    for (int n = 1; n <= 42; ++n) {
        chase.Tick(rules, target, tick);
        loaded.Tick(rules, target, Hud::HandOver(rules, n * tick, tick, owed));
        if (n == 27) CHECK_MSG(loaded.at == placed, "held where the level put it while the blacks are whole");
    }
    const double chaseOff = glm::length(chase.at - target);
    const double loadedOff = glm::length(loaded.at - target);
    CHECK_MSG(chaseOff > 12.0 && loadedOff < 3.5,
              "at 700 ms: " + Num(chaseOff) + " units out frame by frame, " + Num(loadedOff) + " handed the load");
}

void TheCaptionFadesOverThreeSeconds() {
    const Hud::Rules rules = Load();
    CHECK(Hud::CaptionText(rules, 0) == "Part 1");
    CHECK(Hud::CaptionText(rules, 16) == "Part 17");
    CHECK_EQ(Hud::CaptionAlpha(rules, 0.0), 1.0);
    CHECK_EQ(Hud::CaptionAlpha(rules, 1500.0), 127.0 / 255.0);
    CHECK_EQ(Hud::CaptionAlpha(rules, 3000.0), 0.0);
    // The library's t2.0 frames, whose caption alphas date them: 0.17..0.21 at
    // about 2.4..2.5 s into the level (level_start_timeline.md, cross-checks).
    CHECK_MSG(Near(Hud::CaptionAlpha(rules, 2445.0), 0.185, 0.01), "1-01's t2.0 caption");
}

void ThePlaqueHoldsThenGoes() {
    const Hud::Rules rules = Load();
    CHECK_EQ(Hud::PlaqueAlpha(rules, 0.0), 0.0);
    // UISprite's appearance: smoothEnd over 1000 ms.
    CHECK_EQ(Hud::PlaqueAlpha(rules, 500.0), static_cast<int>(std::sin(0.25 * 3.141592653589793) * 255.0) / 255.0);
    CHECK_EQ(Hud::PlaqueAlpha(rules, 1000.0), 1.0);
    CHECK_EQ(Hud::PlaqueAlpha(rules, 2000.0), 1.0); // getUiTime() > 2000, strictly
    CHECK_MSG(Hud::PlaqueAlpha(rules, 2017.0) < 1.0, "and going the tick after");
    CHECK_EQ(Hud::PlaqueAlpha(rules, 3000.0), 0.0);
    bool falling = true;
    for (double ms = 2000.0; ms < 3000.0; ms += 16.0) {
        if (Hud::PlaqueAlpha(rules, ms + 16.0) > Hud::PlaqueAlpha(rules, ms)) falling = false;
    }
    CHECK_MSG(falling, "the dismissal only ever fades");
    // Against the library's t2.0 frames, which were measured at plaque 0.40..0.49
    // with captions dating them to 2.39..2.45 s. The decoded 2000 ms runs a little
    // under them, which step 38 records rather than tunes away.
    const double at1_05 = Hud::PlaqueAlpha(rules, 2385.0);
    CHECK_MSG(Near(at1_05, 0.488, 0.08), "1-05's t2.0 plaque, measured 0.488, decoded " + Num(at1_05));

    const glm::dvec2 plaqueTopLeft = rules.plaque.centreUnits - rules.plaque.sizeUnits * 0.5;
    const glm::dvec2 medalTopLeft = rules.plaque.medalCentreUnits - rules.plaque.medalSizeUnits * 0.5;
    CHECK_MSG(glm::length(plaqueTopLeft - glm::dvec2(8.02, 4.02)) < 0.05,
              "the plaque's top-left is the measured (8.02, 4.02)");
    CHECK_MSG(glm::length(medalTopLeft - glm::dvec2(8.02, 8.02)) < 0.05,
              "the medal's top-left is the measured (8.02, 8.02)");

    CHECK(Hud::MedalSprite(rules, 0).empty());
    CHECK(Hud::MedalSprite(rules, 1) == "medal_bronze_l.png");
    CHECK(Hud::MedalSprite(rules, 2) == "medal_silver_l.png");
    CHECK(Hud::MedalSprite(rules, 3) == "medal_gold_l.png");
}

void TheCaptionIsLaidOutAsTheOriginalLaysItOut() {
    const Hud::Rules rules = Load();
    // A font of this suite's own: 'P' on page 0 and 'a' on page 1, advances 57
    // and 31, a line 84 high - Matura84's own numbers for those two letters, so
    // the arithmetic is the one the real caption gets.
    const std::string fnt = Write("caption.fnt",
                                  "info face=\"Test\" size=84\n"
                                  "common lineHeight=84 base=54 scaleW=512 scaleH=512 pages=2\n"
                                  "page id=0 file=\"caption_0.png\"\n"
                                  "page id=1 file=\"caption_1.png\"\n"
                                  "char id=80 x=256 y=395 width=65 height=68 xoffset=0 yoffset=11 xadvance=57 page=0\n"
                                  "char id=97 x=329 y=98 width=38 height=38 xoffset=-1 yoffset=26 xadvance=31 page=1\n"
                                  "kerning first=80 second=97 amount=1\n");
    Supersonic::BitmapFont font;
    std::string error;
    CHECK_MSG(font.Load(fnt, error), error);
    const std::vector<Hud::Glyph> glyphs = Hud::LayOutCaption(rules, font, "Pa", kView720);
    CHECK_EQ(glyphs.size(), std::size_t{2});
    if (glyphs.size() != 2) return;

    // The box is (57 + 31) x 84 font px at half a unit each, centred on
    // (0.5, 0.8) of the view - and no kerning moves the 'a', because gs2d applies
    // none.
    const glm::dvec2 box(44.0, 42.0);
    const glm::dvec2 origin = glm::dvec2(0.5, 0.8) * kView720 - box * 0.5;
    CHECK_MSG(glm::length(glyphs[0].rect.min - (origin + glm::dvec2(0.0, 5.5))) < 1e-9, "P at its offsets");
    CHECK_MSG(glm::length(glyphs[1].rect.min - (origin + glm::dvec2(28.0, 13.0))) < 1e-9,
              "a at the pen 57 plus xoffset -1, halved, with no kerning");
    CHECK(glyphs[0].rect.size == glm::dvec2(32.5, 34.0));
    CHECK_EQ(glyphs[0].page, 0);
    CHECK_EQ(glyphs[1].page, 1);
    CHECK(glyphs[1].uvOffset == glm::dvec2(329.0 / 512.0, 98.0 / 512.0));
    CHECK(glyphs[1].uvScale == glm::dvec2(38.0 / 512.0, 38.0 / 512.0));
    // The line's centre is 0.8 of the view down: level_start_timeline.md measured
    // 204.47 units against 204.8.
    CHECK_MSG(Near(origin.y + box.y * 0.5, 204.8, 1e-9) && Near(204.8, 204.47, 0.34),
              "the line's centre sits on 0.8 of the height, 0.33 units from the measurement");

    Supersonic::BitmapFont unloaded;
    CHECK(Hud::LayOutCaption(rules, unloaded, "Pa", kView720).empty());
}

void WhatIsRefused() {
    Hud::Rules rules;
    std::string error;
    CHECK(!Hud::LoadRules(Write("missing.json", "{}"), rules, error));
    CHECK_MSG(!error.empty(), "an empty file says why");

    // Everything but a bad anchor, so the anchor is what is refused.
    std::ifstream in(kUi, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string needle = "\"anchor\": \"top_right\"";
    const std::size_t at = text.find(needle);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    std::string bad = text;
    bad.replace(at, needle.size(), "\"anchor\": \"top_middle\"");
    error.clear();
    CHECK(!Hud::LoadRules(Write("anchor.json", bad), rules, error));
    CHECK_MSG(error.find("anchor") != std::string::npos, "a bad anchor is named: " + error);

    std::string loud = text;
    const std::string alpha = "\"alpha_byte\": 120";
    loud.replace(loud.find(alpha), alpha.size(), "\"alpha_byte\": 200");
    error.clear();
    CHECK_MSG(!Hud::LoadRules(Write("alpha.json", loud), rules, error),
              "a pulse that would carry 200 + 90 past a byte is refused: " + error);

    std::string early = text;
    const std::string after = "\"start_after_ms\": 465";
    CHECK(early.find(after) != std::string::npos);
    if (early.find(after) == std::string::npos) return;
    early.replace(early.find(after), after.size(), "\"start_after_ms\": -1");
    error.clear();
    CHECK_MSG(!Hud::LoadRules(Write("early.json", early), rules, error) &&
                  error.find("start_after_ms") != std::string::npos,
              "a black that would start before its level is refused, by name: " + error);
}

void runTests() {
    TheFileSaysWhatWasMeasuredAndDecoded();
    TheButtonsSitWhereTheCapturesPutThem();
    ThePadsSlideInAndSitInTheCorners();
    ThePadsPulseAsComputeButtonColorSays();
    TheTutorialRingGrowsAndFades();
    TheLevelOpensInTheDark();
    TheOpeningIsRec11sFrameByFrame();
    ThePadsSitBetweenTheTwoBlacks();
    TheNoPortalSignFollowsTheCameraCorner();
    TheSignIsHandedTheLoadInOnePiece();
    TheCaptionFadesOverThreeSeconds();
    ThePlaqueHoldsThenGoes();
    TheCaptionIsLaidOutAsTheOriginalLaysItOut();
    WhatIsRefused();
}

} // namespace

TEST_MAIN("test_mp_hud", 100)
