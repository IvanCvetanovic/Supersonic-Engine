#include "sim/Art.hpp"

#include "core/DetMath.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>

namespace MagicPortals::Art {

namespace {

namespace Json = Supersonic::Json;

bool WholeBelow(const Json::Value& value, int below, int& out) {
    if (!value.IsNumber()) return false;
    const double n = value.AsNumber();
    if (n < 0.0 || n != std::floor(n) || n >= below) return false;
    out = static_cast<int>(n);
    return true;
}

bool WholeAtLeastOne(const Json::Value& value, int& out) {
    if (!value.IsNumber()) return false;
    const double n = value.AsNumber();
    if (n < 1.0 || n != std::floor(n) || n > 64.0) return false;
    out = static_cast<int>(n);
    return true;
}

bool Pair(const Json::Value& value, glm::dvec2& out) {
    if (!value.IsArray() || value.AsArray().size() != 2 || !value.AsArray()[0].IsNumber() ||
        !value.AsArray()[1].IsNumber()) {
        return false;
    }
    out = glm::dvec2(value.AsArray()[0].AsNumber(), value.AsArray()[1].AsNumber());
    return true;
}

// Three numbers, none below zero: an .ent's <EmissiveColor> r, g and b.
bool Emissive(const Json::Value& value, glm::dvec3& out) {
    if (!value.IsArray() || value.AsArray().size() != 3) return false;
    for (std::size_t i = 0; i < 3; ++i) {
        const Json::Value& channel = value.AsArray()[i];
        if (!channel.IsNumber() || !(channel.AsNumber() >= 0.0)) return false;
        out[static_cast<glm::length_t>(i)] = channel.AsNumber();
    }
    return true;
}

bool Finite(const Json::Value& value) {
    return value.IsNumber() && std::isfinite(value.AsNumber());
}

bool Numbers(const Json::Value& value, std::size_t count, double* out) {
    if (!value.IsArray() || value.AsArray().size() != count) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!Finite(value.AsArray()[i])) return false;
        out[i] = value.AsArray()[i].AsNumber();
    }
    return true;
}

// A picture's <Light>, every number stated: a range above zero (ETHEntityProperties.cpp
// keeps no other light), the offset in three, a colour of three none below zero,
// and its halo - a file, where it stands, how big and how bright. And the depth its
// height is measured from, which the light cannot be placed without.
bool ReadLight(const Json::Value& entry, const Json::Value& z, const std::string& name, Picture& into,
               std::string& error) {
    Lighting::Light light;
    double offset[3] = {0.0, 0.0, 0.0};
    double colour[3] = {0.0, 0.0, 0.0};
    double haloOffset[2] = {0.0, 0.0};
    double haloSize[2] = {0.0, 0.0};
    const bool ok = entry.IsObject() && Finite(entry["range"]) && entry["range"].AsNumber() > 0.0 &&
                    Numbers(entry["offset"], 3, offset) && Numbers(entry["colour"], 3, colour) &&
                    colour[0] >= 0.0 && colour[1] >= 0.0 && colour[2] >= 0.0 && entry["halo"].IsString() &&
                    !entry["halo"].AsString("").empty() && Numbers(entry["halo_offset"], 2, haloOffset) &&
                    Numbers(entry["halo_size"], 2, haloSize) && haloSize[0] > 0.0 && haloSize[1] > 0.0 &&
                    Finite(entry["halo_brightness"]) && entry["halo_brightness"].AsNumber() >= 0.0;
    if (!ok) {
        error = name + "'s light needs a range above 0, offset, colour, halo, halo_offset, halo_size and "
                       "halo_brightness";
        return false;
    }
    if (!z.IsObject() || !Finite(z["value"])) {
        error = name + " has a light and no z.value to measure its height from";
        return false;
    }
    light.range = entry["range"].AsNumber();
    light.offset = glm::dvec3(offset[0], offset[1], offset[2]);
    light.colour = glm::dvec3(colour[0], colour[1], colour[2]);
    light.halo = entry["halo"].AsString("");
    light.haloOffset = glm::dvec2(haloOffset[0], haloOffset[1]);
    light.haloSize = glm::dvec2(haloSize[0], haloSize[1]);
    light.haloBrightness = entry["halo_brightness"].AsNumber();
    into.light = light;
    into.z = z["value"].AsNumber();
    return true;
}

// `plays` false for a sheet whose frames are chosen rather than played, which
// then need not say how fast.
bool ReadPicture(const Json::Value& entry, const std::string& name, Picture& out, std::string& error,
                 bool plays = true) {
    if (!entry.IsObject() || !entry.Has("sprite") || !entry["sprite"].IsString() || !entry.Has("additive") ||
        !entry["additive"].IsBool()) {
        error = name + " needs sprite and additive";
        return false;
    }
    Picture read;
    read.sprite = entry["sprite"].AsString("");
    read.additive = entry["additive"].AsBool();
    // Required: a picture without one would be drawn at the ambient alone, which
    // for the player on a dark level is black - a default nobody decoded.
    if (!Emissive(entry["emissive"], read.emissive)) {
        error = name + "'s emissive is not three numbers, none below zero";
        return false;
    }
    // Which lights reach it, and none by default: a picture that said nothing
    // would be unlit by a rule nobody read out of its .ent.
    if (!entry.Has("static") || !entry["static"].IsBool() || !entry.Has("apply_light") ||
        !entry["apply_light"].IsBool()) {
        error = name + " needs static and apply_light, each true or false";
        return false;
    }
    read.isStatic = entry["static"].AsBool();
    read.applyLight = entry["apply_light"].AsBool();
    if (entry.Has("normal")) {
        if (!entry["normal"].IsString() || entry["normal"].AsString("").empty()) {
            error = name + "'s normal is not a file name";
            return false;
        }
        read.normal = entry["normal"].AsString("");
    }
    if (entry.Has("light") && !ReadLight(entry["light"], entry["z"], name, read, error)) return false;
    if ((entry.Has("columns") && !WholeAtLeastOne(entry["columns"], read.columns)) ||
        (entry.Has("rows") && !WholeAtLeastOne(entry["rows"], read.rows))) {
        error = name + "'s columns and rows are whole numbers from 1";
        return false;
    }
    // A sheet of more than one frame plays, so it has to say how fast.
    if (plays && read.Frames() > 1) {
        const Json::Value& animation = entry["animation"];
        if (!animation.IsObject() || !animation.Has("frames_per_second") ||
            !animation["frames_per_second"].IsNumber() || animation["frames_per_second"].AsNumber() <= 0.0) {
            error = name + " has " + std::to_string(read.Frames()) +
                    " frames and no animation.frames_per_second above zero";
            return false;
        }
        read.framesPerSecond = animation["frames_per_second"].AsNumber();
    }
    out = std::move(read);
    return true;
}

bool ReadPulse(const Json::Value& entry, Pulse& out) {
    Pulse read;
    if (!entry.IsObject() || !Pair(entry["from"], read.fromScale) || !Pair(entry["to"], read.toScale) ||
        !entry["stride_ms"].IsNumber() || entry["stride_ms"].AsNumber() <= 0.0) {
        return false;
    }
    read.strideMs = entry["stride_ms"].AsNumber();
    out = read;
    return true;
}

} // namespace

glm::dvec2 Pulse::ScaleAt(double elapsedMs) const {
    // bounce(): the count of strides so far says which way it is going, and how
    // far through this one it is, eased by smoothEnd, says where.
    const double strides = std::floor(elapsedMs / strideMs);
    double bias = (elapsedMs - strides * strideMs) / strideMs;
    if (std::fmod(strides, 2.0) == 1.0) bias = 1.0 - bias;
    const double eased = Supersonic::DetMath::sin(static_cast<float>(bias) * 1.570796327f);
    return fromScale + (toScale - fromScale) * eased;
}

glm::dvec3 Antiportal::ColourAt(double elapsedMs) const {
    if (!(strideMs > 0.0)) return from;
    // blinkColor ins 55-75, in the order it runs them: which leg this is
    // (elapsedTime / stride), which way it runs (that count odd), and how far along
    // it is (the remainder over the stride). The two clocks do NOT run at one rate: the
    // original sums a FLOOR of each frame's milliseconds as a uint (ETHEngine.cpp:147
    // truncates the loop's float, and libApplication.so's Update(float) calls
    // __aeabi_f2uiz before SetLastFrameElapsedTime), so at 60 Hz it gains 16 ms a frame
    // where this port's tick gains 1000/60 - the port runs 4.17 % fast. It is the clock
    // every scripted motion reads, so flooring it is a port-wide change and not this
    // function's: see art.json antiportal _source and the record's step 84.
    const double ms = std::max(0.0, elapsedMs);
    const double legs = std::floor(ms / strideMs);
    double bias = (ms - legs * strideMs) / strideMs;
    if (std::fmod(legs, 2.0) == 1.0) bias = 1.0 - bias;
    return from + (to - from) * bias;
}

int Timer::FrameAt(double elapsedMs, double timeMs) const {
    if (!(timeMs > 0.0)) return frames - 1;
    // uTOf, DIVf, MULIf 8f and fTOi (ETHCallback_timer ins 233-239), then
    // max(frame, 0) and min(7) (ins 242-248): single precision, as the script's
    // floats are, so a cell turns on the same millisecond.
    const float at = static_cast<float>(elapsedMs) / static_cast<float>(timeMs) * static_cast<float>(frames);
    return static_cast<int>(std::clamp(at, 0.0f, static_cast<float>(frames - 1)));
}

double Timer::LegMs(double elapsedMs, double timeMs) const {
    return std::max(pulseMinLegMs, timeMs - elapsedMs);
}

glm::dvec2 Timer::PulseAt(double elapsedMs, double timeMs) const {
    // bounce(thisEntity, V2_ONE, vector2(1.15, 1.15), max(400, timeLeft)) (ins
    // 204-231): its blinkElapsedTime starts at 0 and takes the same frame times
    // as elapsedTime, so the two are one clock. Only the leg changes each frame.
    Pulse pulse;
    pulse.fromScale = glm::dvec2(pulseFrom);
    pulse.toScale = glm::dvec2(pulseTo);
    pulse.strideMs = LegMs(elapsedMs, timeMs);
    return pulse.ScaleAt(elapsedMs);
}

double Timer::DecayOver(double seconds) const {
    return std::pow(shrinkPerFrame, seconds * decayFramesPerSecond);
}

bool Timer::Gone(double scaleX) const {
    // GetScale().x < 0.1f (ETHCallback_timer ins 184-199). GetScale is what
    // SetScale stored, and bounce stored g_scale.scale(the pulse) (ins 90-102):
    // the port's scale x m_scaleFactor. Only this test is in those units; the
    // port draws the cell at its own.
    return scaleX * screenPxPerUnit < goneBelowScale;
}

double NoGravityMotion::HoverUnits(float angle, double viewUnitsTall) const {
    // SetPivotAdjust(originalPivotAdjust + vector2(0, cos(angle) * 1.2)): screen
    // pixels, since the pivot is kept times the entity's scale and drawn times it
    // again; units as a screen hoverAtScreenPx tall shows them.
    return hoverScreenPx * static_cast<double>(Supersonic::DetMath::cos(angle)) * viewUnitsTall / hoverAtScreenPx;
}

const std::string& PlayerSheet(const Character& mage, bool noGravity) {
    return noGravity ? mage.noGravitySprite : mage.sprite;
}

int FrameTimer::Set(int firstFrame, int lastFrame, double strideMs, bool repeat, double elapsedMs) {
    timeMs += elapsedMs;
    if (firstFrame != first || lastFrame != last) {
        frame = firstFrame;
        first = firstFrame;
        last = lastFrame;
        timeMs = 0.0;
        return frame;
    }
    if (timeMs >= strideMs) {
        ++frame;
        timeMs -= strideMs;
        if (frame > last) {
            if (repeat) {
                frame = first;
            } else {
                frame = last;
                timeMs = 0.0;
            }
        }
    }
    return frame;
}

int UpdateNoGravity(const Character& mage, NoGravityPlayer& player, bool walking, double elapsedMs) {
    const int lastColumn = mage.columns - 1;
    // SideScrollerCharacter::updateFrame: a walk draws its own set; a stand draws
    // the idle column as the update before left it.
    const int drawn = walking ? player.timer.Set(0, lastColumn, mage.strideMs, true, elapsedMs) : player.idleColumn;
    // MainCharacter::linearMotion.
    const NoGravityMotion& motion = mage.noGravity;
    if (!player.moved) {
        player.angle = static_cast<float>(motion.hoverStartRadians);
        player.moved = true;
    }
    player.angle += static_cast<float>(motion.hoverRadiansPerSecond * elapsedMs / 1000.0);
    if (player.angle > static_cast<float>(motion.hoverWrapRadians)) {
        player.angle -= static_cast<float>(motion.hoverWrapRadians);
    }
    player.idleColumn = player.timer.Set(0, lastColumn, motion.columnStrideMs, true, elapsedMs);
    return drawn;
}

bool LoadRules(const std::string& path, Rules& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot open";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    Json::Parser parser(text);
    Json::Value root;
    if (!parser.Parse(root)) {
        error = path + ": " + parser.Error();
        return false;
    }
    if (!root.IsObject() || !root.Has("portal") || !root.Has("shot") || !root.Has("torch_light") ||
        !root.Has("character")) {
        error = path + ": portal, shot, torch_light and character are each an object";
        return false;
    }
    Rules read;
    std::string why;
    if (!ReadPicture(root["portal"], "portal", read.portal, why) ||
        !ReadPicture(root["shot"], "shot", read.shot, why) ||
        !ReadPicture(root["torch_light"], "torch_light", read.torchLight, why, false) ||
        // The player's rate is its stride, read with the rest of its walk below.
        !ReadPicture(root["character"], "character", static_cast<Picture&>(read.character), why, false)) {
        error = path + ": " + why;
        return false;
    }
    // What the player's sheet adds: which way it faces when it is made, where it
    // stands, which row walks which way, which column it stands on, and a
    // column's time. None has a default: a start facing or a stride nobody
    // decoded is the picture the footage refused.
    const Json::Value& character = root["character"];
    Character& mage = read.character;
    const Json::Value& pivot = character["pivot_px"];
    const Json::Value& rowsBy = character["rows_by_direction"];
    const Json::Value& walk = character["animation"];
    if (!character.Has("initial_direction") ||
        !WholeBelow(character["initial_direction"], mage.rows, mage.initialDirection) || !pivot.IsArray() ||
        pivot.AsArray().size() != 2 || !pivot.AsArray()[0].IsNumber() || !pivot.AsArray()[1].IsNumber() ||
        !rowsBy.IsObject() || !rowsBy.Has("left") || !WholeBelow(rowsBy["left"], mage.rows, mage.leftRow) ||
        !rowsBy.Has("right") || !WholeBelow(rowsBy["right"], mage.rows, mage.rightRow) || !walk.IsObject() ||
        !walk.Has("idle_column") || !WholeBelow(walk["idle_column"], mage.columns, mage.idleColumn) ||
        !Finite(walk["stride_ms"]) || !(walk["stride_ms"].AsNumber() > 0.0)) {
        error = path + ": character needs initial_direction, pivot_px, rows_by_direction.left and .right and "
                       "animation.idle_column, each inside its sheet, and animation.stride_ms above 0";
        return false;
    }
    mage.pivotXPx = pivot.AsArray()[0].AsNumber();
    mage.pivotYPx = pivot.AsArray()[1].AsNumber();
    mage.strideMs = walk["stride_ms"].AsNumber();
    mage.framesPerSecond = 1000.0 / mage.strideMs;

    // Its arm out: the ray detectPushing casts and the rows findFinalDirection
    // draws while it meets something. None defaulted - without them a wall is
    // walked into on the walking row, which the decode refuses.
    const Json::Value& push = character["push"];
    const Json::Value& pushRows = push["rows"];
    Push& arm = mage.push;
    double offset[2] = {0.0, 0.0};
    if (!push.IsObject() || !Finite(push["reach_frame_width_share"]) ||
        !(push["reach_frame_width_share"].AsNumber() > 0.0) || !Numbers(push["offset_px"], 2, offset) ||
        !Finite(push["air_velocity_share"]) || !(push["air_velocity_share"].AsNumber() > 0.0) ||
        !pushRows.IsObject() || !pushRows.Has("left") || !WholeBelow(pushRows["left"], mage.rows, arm.leftRow) ||
        !pushRows.Has("other") || !WholeBelow(pushRows["other"], mage.rows, arm.otherRow)) {
        error = path + ": character needs push.reach_frame_width_share and push.air_velocity_share above 0, "
                       "push.offset_px as two numbers, and push.rows.left and .other, each inside its sheet";
        return false;
    }
    arm.reachFrameWidthShare = push["reach_frame_width_share"].AsNumber();
    arm.offsetPx = glm::dvec2(offset[0], offset[1]);
    arm.airVelocityShare = push["air_velocity_share"].AsNumber();

    // And what a no_gravity level changes: the sheet it wears, the column that
    // turns while it stands, and its hover. None is defaulted - without them the
    // suit is the sheet the footage refused on 18 levels.
    const Json::Value& weightless = character["no_gravity_motion"];
    const auto positive = [](const Json::Value& value) { return Finite(value) && value.AsNumber() > 0.0; };
    if (!character["no_gravity_sprite"].IsString() || character["no_gravity_sprite"].AsString("").empty() ||
        !weightless.IsObject() || !positive(weightless["column_stride_ms"]) ||
        !positive(weightless["hover_screen_px"]) || !positive(weightless["hover_at_screen_px"]) ||
        !positive(weightless["hover_radians_per_second"]) || !Finite(weightless["hover_start_radians"]) ||
        !positive(weightless["hover_wrap_radians"])) {
        error = path + ": character needs no_gravity_sprite, and no_gravity_motion's column_stride_ms, "
                       "hover_screen_px, hover_at_screen_px, hover_radians_per_second and hover_wrap_radians "
                       "above 0 and a hover_start_radians";
        return false;
    }
    mage.noGravitySprite = character["no_gravity_sprite"].AsString("");
    NoGravityMotion& motion = mage.noGravity;
    motion.columnStrideMs = weightless["column_stride_ms"].AsNumber();
    motion.hoverScreenPx = weightless["hover_screen_px"].AsNumber();
    motion.hoverAtScreenPx = weightless["hover_at_screen_px"].AsNumber();
    motion.hoverRadiansPerSecond = weightless["hover_radians_per_second"].AsNumber();
    motion.hoverStartRadians = weightless["hover_start_radians"].AsNumber();
    motion.hoverWrapRadians = weightless["hover_wrap_radians"].AsNumber();

    // Chapter 1's boss and its spikes, whose frames are chosen, not played.
    if (!root.Has("beholder") || !root.Has("spike")) {
        error = path + ": beholder and spike are each an object";
        return false;
    }
    if (!ReadPicture(root["beholder"], "beholder", static_cast<Picture&>(read.beholder), why, false) ||
        !ReadPicture(root["spike"], "spike", static_cast<Picture&>(read.spike), why, false)) {
        error = path + ": " + why;
        return false;
    }
    const Json::Value& pulse = root["beholder"]["pulse"];
    if (!pulse.IsObject() || !ReadPulse(pulse["seeking"], read.beholder.seeking) ||
        !ReadPulse(pulse["hurt"], read.beholder.hurt) || !ReadPulse(pulse["dead"], read.beholder.dead)) {
        error = path + ": beholder.pulse needs seeking, hurt and dead, each with from, to and a stride_ms above 0";
        return false;
    }
    glm::dvec2 spikePivot(0.0);
    if (!Pair(root["spike"]["pivot_px"], spikePivot)) {
        error = path + ": spike.pivot_px is not two numbers";
        return false;
    }
    read.spike.pivotXPx = spikePivot.x;
    read.spike.pivotYPx = spikePivot.y;

    // A static portal as its callback redraws it. Every number is the bytecode's,
    // and none has a default: a scale of 1 or a white tint would be the port's
    // old picture, which is the one the footage refuses.
    const Json::Value& portalStatic = root["static_portal"];
    StaticPortal& statics = read.staticPortal;
    if (!portalStatic.IsObject() || !portalStatic["entity"].IsString() || portalStatic["entity"].AsString("").empty() ||
        !Finite(portalStatic["scale"]) || !(portalStatic["scale"].AsNumber() > 0.0) || !portalStatic["red"].IsString() ||
        portalStatic["red"].AsString("").empty() || !Emissive(portalStatic["tint_red"], statics.tintRed) ||
        !Emissive(portalStatic["tint_otherwise"], statics.tintOtherwise)) {
        error = path + ": static_portal needs entity, a scale above 0, red, and tint_red and tint_otherwise, "
                       "each three numbers none below 0";
        return false;
    }
    statics.entity = portalStatic["entity"].AsString("");
    statics.scale = portalStatic["scale"].AsNumber();
    statics.red = portalStatic["red"].AsString("");

    // The ring round a no-portal field as its callback blinks and turns it. None of
    // it has a default either: without the blink the port draws the white ring the
    // footage refuses, and a spin of 0 would be a picture the script turns and the
    // port does not.
    const Json::Value& anti = root["antiportal"];
    Antiportal& antiportal = read.antiportal;
    const Json::Value& blink = anti["blink"];
    if (!anti.IsObject() || !anti["entity"].IsString() || anti["entity"].AsString("").empty() ||
        anti["entity"].AsString("").find('.') != std::string::npos || !blink.IsObject() ||
        !Emissive(blink["from"], antiportal.from) || !Emissive(blink["to"], antiportal.to) ||
        !Finite(blink["stride_ms"]) || !(blink["stride_ms"].AsNumber() > 0.0) ||
        !Finite(anti["spin_deg_per_s"])) {
        error = path + ": antiportal needs entity, a name without a dot, and blink with from and to, each three "
                       "numbers none below 0, a stride_ms above 0 and a finite spin_deg_per_s";
        return false;
    }
    antiportal.entity = anti["entity"].AsString("");
    antiportal.strideMs = blink["stride_ms"].AsNumber();
    antiportal.spinDegPerS = anti["spin_deg_per_s"].AsNumber();

    // The dial behind a timed crystal: timer.ent's picture, whose cell is chosen
    // rather than played, and the numbers its script runs it by. None has a
    // default: without them the port would draw no dial, or the fade the
    // original never had.
    if (!root.Has("timer")) {
        error = path + ": timer is an object";
        return false;
    }
    const Json::Value& timerEntry = root["timer"];
    if (!ReadPicture(timerEntry, "timer", static_cast<Picture&>(read.timer), why, false)) {
        error = path + ": " + why;
        return false;
    }
    Timer& timer = read.timer;
    const auto between = [&timerEntry](const char* key, double above, double atMost) {
        return Finite(timerEntry[key]) && timerEntry[key].AsNumber() > above && timerEntry[key].AsNumber() <= atMost;
    };
    const Json::Value& decayRate = timerEntry["decay_frames_per_second"];
    const bool clockOk = WholeAtLeastOne(timerEntry["frames"], timer.frames) && timer.frames <= timer.Frames() &&
                         between("alpha", 0.0, 1.0) && Finite(timerEntry["z_offset"]) &&
                         timerEntry["z_offset"].AsNumber() == std::floor(timerEntry["z_offset"].AsNumber()) &&
                         between("pulse_from", 0.0, 1e9) && between("pulse_to", 0.0, 1e9) &&
                         between("pulse_min_leg_ms", 0.0, 1e9) && between("shrink_per_frame", 0.0, 1.0) &&
                         timerEntry["shrink_per_frame"].AsNumber() < 1.0 && between("gone_below_scale", 0.0, 1.0) &&
                         timerEntry["gone_below_scale"].AsNumber() < 1.0 && decayRate.IsObject() &&
                         Finite(decayRate["value"]) && decayRate["value"].AsNumber() > 0.0 &&
                         between("screen_px_per_unit", 0.0, 1e9);
    if (!clockOk) {
        error = path + ": timer needs frames from 1 to its cells, alpha above 0 and at most 1, a whole z_offset, "
                       "pulse_from, pulse_to and pulse_min_leg_ms above 0, shrink_per_frame and gone_below_scale "
                       "between 0 and 1, decay_frames_per_second.value above 0 and screen_px_per_unit above 0";
        return false;
    }
    timer.alpha = timerEntry["alpha"].AsNumber();
    timer.zOffset = static_cast<int>(timerEntry["z_offset"].AsNumber());
    timer.pulseFrom = timerEntry["pulse_from"].AsNumber();
    timer.pulseTo = timerEntry["pulse_to"].AsNumber();
    timer.pulseMinLegMs = timerEntry["pulse_min_leg_ms"].AsNumber();
    timer.shrinkPerFrame = timerEntry["shrink_per_frame"].AsNumber();
    timer.decayFramesPerSecond = decayRate["value"].AsNumber();
    timer.goneBelowScale = timerEntry["gone_below_scale"].AsNumber();
    timer.screenPxPerUnit = timerEntry["screen_px_per_unit"].AsNumber();
    out = std::move(read);
    return true;
}

} // namespace MagicPortals::Art
