#include "core/InputRecording.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Supersonic {

namespace {

// The first line, so a file that is not one of these says so immediately
// rather than failing on some later line for a reason that reads like a bug in
// the parser.
constexpr const char* kMagic = "SUPERSONICREPLAY";
constexpr int kFormatVersion = 1;

// --- floats, by their bits -------------------------------------------------
//
// See the note in the header. memcpy rather than a union or a reinterpret_cast:
// it is the only one of the three that is not undefined behaviour, and every
// compiler folds it to nothing.

uint32_t bitsOf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float floatFrom(uint32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string hex(uint64_t value, int digits) {
    static const char* kDigits = "0123456789abcdef";
    std::string out(static_cast<size_t>(digits), '0');
    for (int i = digits - 1; i >= 0; --i) {
        out[static_cast<size_t>(i)] = kDigits[value & 0xfull];
        value >>= 4;
    }
    return out;
}

// Strict: the whole token must be hex and no longer than the width asked for.
// std::stoull would accept "12zz" and a leading "0x", and both of those are a
// corrupt file being read as a valid one.
bool parseHex(const std::string& token, int maxDigits, uint64_t& out) {
    if (token.empty() || static_cast<int>(token.size()) > maxDigits) return false;
    uint64_t value = 0;
    for (const char c : token) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<uint64_t>(c - 'A' + 10);
        else return false;
    }
    out = value;
    return true;
}

// Same strictness for the decimal counts, and for the same reason.
bool parseIndex(const std::string& token, uint64_t& out) {
    if (token.empty() || token.size() > 20) return false;
    uint64_t value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    out = value;
    return true;
}

// "3,7,12" -> {3, 7, 12}. An empty list is written as "-" rather than as
// nothing, so a segment can express "this changed to empty" without the parser
// having to guess whether the next word is a value or the next keyword.
bool splitList(const std::string& token, std::vector<uint64_t>& out) {
    out.clear();
    if (token == "-") return true;

    size_t start = 0;
    while (start <= token.size()) {
        const size_t comma = token.find(',', start);
        const std::string piece = token.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        uint64_t value = 0;
        if (!parseIndex(piece, value)) return false;
        out.push_back(value);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return true;
}

std::string joinList(const std::vector<uint64_t>& values) {
    if (values.empty()) return "-";
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(values[i]);
    }
    return out;
}

// The indices of `names` that appear in `present`, in name order.
//
// Sorted by the name table's own order rather than by whatever order the
// capture produced, so two recordings of the same input are the same bytes.
std::vector<uint64_t> indicesOf(const std::vector<std::string>& present,
                                const std::unordered_map<std::string, uint64_t>& indexByName) {
    std::vector<uint64_t> out;
    out.reserve(present.size());
    for (const std::string& name : present) {
        const auto it = indexByName.find(name);
        if (it != indexByName.end()) out.push_back(it->second);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> namesOf(const std::vector<uint64_t>& indices,
                                 const std::vector<std::string>& names) {
    std::vector<std::string> out;
    out.reserve(indices.size());
    for (const uint64_t index : indices) {
        if (index < names.size()) out.push_back(names[static_cast<size_t>(index)]);
    }
    return out;
}

// Every action and axis the recording mentions anywhere, in first-seen order.
//
// Built from the ticks rather than from Input::ActionNames(), because a
// recording has to be writable from a value - a test builds ticks by hand and
// never binds anything - and because the file must name what it actually
// contains rather than what the process happened to have bound when it was
// saved.
void collectNames(const InputRecording& recording, std::vector<std::string>& actions,
                  std::vector<std::string>& axes) {
    auto add = [](std::vector<std::string>& into, const std::string& name) {
        if (std::find(into.begin(), into.end(), name) == into.end()) into.push_back(name);
    };

    for (const Input::TickInput& tick : recording.ticks) {
        for (const std::string& name : tick.down) add(actions, name);
        for (const std::string& name : tick.pressed) add(actions, name);
        for (const std::string& name : tick.released) add(actions, name);
        for (const auto& [name, value] : tick.axes) add(axes, name);
    }
}

} // namespace

std::string InputRecording::Write(const InputRecording& recording) {
    std::ostringstream out;

    std::vector<std::string> actionNames;
    std::vector<std::string> axisNames;
    collectNames(recording, actionNames, axisNames);

    std::unordered_map<std::string, uint64_t> actionIndex;
    for (size_t i = 0; i < actionNames.size(); ++i) actionIndex[actionNames[i]] = i;
    std::unordered_map<std::string, uint64_t> axisIndex;
    for (size_t i = 0; i < axisNames.size(); ++i) axisIndex[axisNames[i]] = i;

    out << kMagic << ' ' << kFormatVersion << '\n';
    if (!recording.scenePath.empty()) out << "scene " << recording.scenePath << '\n';
    out << "step " << hex(bitsOf(recording.fixedDelta), 8) << '\n';
    out << "ticks " << recording.ticks.size() << '\n';

    // One name per line, rest-of-line, so a name containing a space is not a
    // format error waiting to happen. Nothing stops a game binding "Move Left".
    for (const std::string& name : actionNames) out << "action " << name << '\n';
    for (const std::string& name : axisNames) out << "axis " << name << '\n';

    for (const ReplayCheckpoint& point : recording.checkpoints) {
        out << "checkpoint " << point.tick << ' ' << hex(point.hash, 16) << '\n';
    }

    // The previous tick's LEVELS, which is what the delta is against. Edges are
    // never carried, so they are not here.
    std::vector<uint64_t> previousDown;
    std::unordered_map<uint64_t, uint32_t> previousAxis;
    bool first = true;

    for (size_t i = 0; i < recording.ticks.size(); ++i) {
        const Input::TickInput& tick = recording.ticks[i];
        std::ostringstream line;

        // Not forced on the first tick, unlike the axes below. Both sides start
        // from "nothing held", so a run whose first tick holds nothing agrees
        // without being told - and writing it anyway would put a line on tick
        // zero of every recording that begins at rest, which is all of them.
        const std::vector<uint64_t> down = indicesOf(tick.down, actionIndex);
        if (down != previousDown) {
            line << " down " << joinList(down);
            previousDown = down;
        }

        // Only the axes whose bits moved - except on the first tick, where
        // every axis is written whatever it reads. That exception is not
        // symmetry with `down` above and is the reason `first` exists at all:
        // a tick's axes are the full set of bound axes including the ones at
        // rest, and the reader only learns an axis belongs in that set when
        // something assigns it. An axis that sat at zero all session and was
        // never written would come back missing rather than zero.
        //
        // After tick zero it costs nothing: an axis nobody touches is written
        // once and never again.
        std::string axisSegment;
        for (const auto& [name, value] : tick.axes) {
            const auto found = axisIndex.find(name);
            if (found == axisIndex.end()) continue;
            const uint32_t bits = bitsOf(value);
            const auto seen = previousAxis.find(found->second);
            if (!first && seen != previousAxis.end() && seen->second == bits) continue;
            if (!axisSegment.empty()) axisSegment += ',';
            axisSegment += std::to_string(found->second) + '=' + hex(bits, 8);
            previousAxis[found->second] = bits;
        }
        if (!axisSegment.empty()) line << " axis " << axisSegment;

        const std::vector<uint64_t> pressed = indicesOf(tick.pressed, actionIndex);
        if (!pressed.empty()) line << " press " << joinList(pressed);

        const std::vector<uint64_t> released = indicesOf(tick.released, actionIndex);
        if (!released.empty()) line << " rel " << joinList(released);

        if (!tick.clicked.empty()) {
            std::vector<uint64_t> clicked(tick.clicked.begin(), tick.clicked.end());
            std::sort(clicked.begin(), clicked.end());
            line << " click " << joinList(clicked);
        }

        // BITS, not value, for the test that decides whether to write it at
        // all. A delta of negative zero compares equal to zero and is a
        // different float, so a value comparison here would quietly drop it and
        // hand back positive zero - making the one part of this file that
        // promises bit-exactness not quite bit-exact, in the one case nobody
        // would think to test.
        if (bitsOf(tick.mouseDelta.x) != 0u || bitsOf(tick.mouseDelta.y) != 0u) {
            line << " mouse " << hex(bitsOf(tick.mouseDelta.x), 8) << ','
                 << hex(bitsOf(tick.mouseDelta.y), 8);
        }

        first = false;

        // A tick that changed nothing and did nothing writes no line at all,
        // which is the whole saving: the reader carries the levels forward and
        // starts the edges empty.
        const std::string body = line.str();
        if (!body.empty()) out << 't' << ' ' << i << body << '\n';
    }

    // The count again, at the end, and this is not redundancy.
    //
    // Delta encoding means a file cut in half is still syntactically perfect -
    // the reader would carry the last levels forward, produce the tick count
    // the header promised, and report success on half a session. The only way
    // to know the file is whole is for it to say where it ends.
    out << "end " << recording.ticks.size() << '\n';

    return out.str();
}

InputRecording InputRecording::Parse(const std::string& text, const std::string& source) {
    InputRecording recording;

    auto fail = [&recording, &source](size_t line, const std::string& what) {
        recording.ok = false;
        recording.error = source + ":" + std::to_string(line) + ": " + what +
                          " (nothing was replayed)";
        return recording;
    };

    std::istringstream in(text);
    std::string rawLine;
    size_t lineNumber = 0;

    bool sawMagic = false;
    bool sawEnd = false;
    bool startedTicks = false;
    uint64_t declaredTicks = 0;
    bool sawTickCount = false;

    std::vector<std::string> actionNames;
    std::vector<std::string> axisNames;

    // The levels carried between tick lines, and the tick they were last
    // written for. Materialising happens once the whole file is read, because
    // the header's tick count may legitimately exceed the last line's index.
    std::vector<uint64_t> currentDown;
    std::unordered_map<uint64_t, uint32_t> currentAxis;

    struct PendingTick {
        uint64_t index{0};
        std::vector<uint64_t> down;
        std::unordered_map<uint64_t, uint32_t> axes;
        std::vector<uint64_t> pressed;
        std::vector<uint64_t> released;
        std::vector<uint64_t> clicked;
        glm::vec2 mouse{0.0f};
    };
    std::vector<PendingTick> pending;
    uint64_t previousTickIndex = 0;
    bool sawAnyTick = false;

    while (std::getline(in, rawLine)) {
        ++lineNumber;

        // A file written on Windows and read on Linux, or the reverse. Cheaper
        // to tolerate here than to explain in a bug report.
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();
        if (rawLine.empty()) continue;

        std::istringstream line(rawLine);
        std::string keyword;
        line >> keyword;

        if (!sawMagic) {
            if (keyword != kMagic) {
                return fail(lineNumber, "not a replay file - expected '" + std::string(kMagic) +
                                            "', got '" + keyword + "'");
            }
            int version = 0;
            if (!(line >> version)) return fail(lineNumber, "the magic line carries no version");
            if (version != kFormatVersion) {
                return fail(lineNumber, "this is a version " + std::to_string(version) +
                                            " replay and this build reads version " +
                                            std::to_string(kFormatVersion));
            }
            sawMagic = true;
            continue;
        }

        if (sawEnd) return fail(lineNumber, "'" + keyword + "' appears after the end marker");

        if (keyword == "t") {
            startedTicks = true;

            std::string indexToken;
            if (!(line >> indexToken)) return fail(lineNumber, "a tick line needs a tick number");
            uint64_t tickIndex = 0;
            if (!parseIndex(indexToken, tickIndex)) {
                return fail(lineNumber, "'" + indexToken + "' is not a tick number");
            }

            // Ascending and unique. Out of order would mean the levels carried
            // forward are the wrong ones, silently.
            if (sawAnyTick && tickIndex <= previousTickIndex) {
                return fail(lineNumber, "tick " + std::to_string(tickIndex) +
                                            " does not come after tick " +
                                            std::to_string(previousTickIndex));
            }
            previousTickIndex = tickIndex;
            sawAnyTick = true;

            PendingTick entry;
            entry.index = tickIndex;

            std::string segment;
            while (line >> segment) {
                std::string value;
                if (!(line >> value)) {
                    return fail(lineNumber, "'" + segment + "' has no value");
                }

                if (segment == "down") {
                    if (!splitList(value, currentDown)) {
                        return fail(lineNumber, "'" + value + "' is not a list of actions");
                    }
                    for (const uint64_t index : currentDown) {
                        if (index >= actionNames.size()) {
                            return fail(lineNumber, "action " + std::to_string(index) +
                                                        " is past the end of the action list");
                        }
                    }
                } else if (segment == "axis") {
                    // "0=3f800000,1=00000000"
                    size_t start = 0;
                    while (start <= value.size()) {
                        const size_t comma = value.find(',', start);
                        const std::string piece = value.substr(
                            start, comma == std::string::npos ? std::string::npos : comma - start);
                        const size_t equals = piece.find('=');
                        if (equals == std::string::npos) {
                            return fail(lineNumber, "'" + piece + "' is not an axis assignment");
                        }
                        uint64_t axisIdx = 0;
                        uint64_t bits = 0;
                        if (!parseIndex(piece.substr(0, equals), axisIdx) ||
                            !parseHex(piece.substr(equals + 1), 8, bits)) {
                            return fail(lineNumber, "'" + piece + "' is not an axis assignment");
                        }
                        if (axisIdx >= axisNames.size()) {
                            return fail(lineNumber, "axis " + std::to_string(axisIdx) +
                                                        " is past the end of the axis list");
                        }
                        currentAxis[axisIdx] = static_cast<uint32_t>(bits);
                        if (comma == std::string::npos) break;
                        start = comma + 1;
                    }
                } else if (segment == "press" || segment == "rel") {
                    std::vector<uint64_t> list;
                    if (!splitList(value, list)) {
                        return fail(lineNumber, "'" + value + "' is not a list of actions");
                    }
                    for (const uint64_t index : list) {
                        if (index >= actionNames.size()) {
                            return fail(lineNumber, "action " + std::to_string(index) +
                                                        " is past the end of the action list");
                        }
                    }
                    if (segment == "press") entry.pressed = std::move(list);
                    else entry.released = std::move(list);
                } else if (segment == "click") {
                    if (!splitList(value, entry.clicked)) {
                        return fail(lineNumber, "'" + value + "' is not a list of entity ids");
                    }
                } else if (segment == "mouse") {
                    const size_t comma = value.find(',');
                    uint64_t x = 0;
                    uint64_t y = 0;
                    if (comma == std::string::npos ||
                        !parseHex(value.substr(0, comma), 8, x) ||
                        !parseHex(value.substr(comma + 1), 8, y)) {
                        return fail(lineNumber, "'" + value + "' is not a mouse delta");
                    }
                    entry.mouse = glm::vec2(floatFrom(static_cast<uint32_t>(x)),
                                            floatFrom(static_cast<uint32_t>(y)));
                } else {
                    return fail(lineNumber, "'" + segment + "' is not something a tick carries");
                }
            }

            entry.down = currentDown;
            entry.axes = currentAxis;
            pending.push_back(std::move(entry));
            continue;
        }

        // Everything below is a header keyword, and a header keyword after the
        // ticks have started is a file somebody has edited into a shape the
        // reader would otherwise interpret half-correctly: an action added
        // after tick 40 would renumber nothing and index everything wrongly.
        if (startedTicks && keyword != "end") {
            return fail(lineNumber, "'" + keyword + "' cannot appear after the first tick");
        }

        if (keyword == "scene") {
            std::getline(line, recording.scenePath);
            if (!recording.scenePath.empty() && recording.scenePath.front() == ' ') {
                recording.scenePath.erase(0, 1);
            }
        } else if (keyword == "step") {
            std::string token;
            uint64_t bits = 0;
            if (!(line >> token) || !parseHex(token, 8, bits)) {
                return fail(lineNumber, "the tick step must be eight hex digits");
            }
            recording.fixedDelta = floatFrom(static_cast<uint32_t>(bits));
            if (!(recording.fixedDelta > 0.0f) || recording.fixedDelta > 1.0f) {
                return fail(lineNumber, "a tick step of " + std::to_string(recording.fixedDelta) +
                                            " seconds is not a step");
            }
        } else if (keyword == "ticks") {
            std::string token;
            if (!(line >> token) || !parseIndex(token, declaredTicks)) {
                return fail(lineNumber, "the tick count must be a whole number");
            }
            sawTickCount = true;
        } else if (keyword == "action") {
            std::string name;
            std::getline(line, name);
            if (!name.empty() && name.front() == ' ') name.erase(0, 1);
            if (name.empty()) return fail(lineNumber, "an action needs a name");
            actionNames.push_back(name);
        } else if (keyword == "axis") {
            std::string name;
            std::getline(line, name);
            if (!name.empty() && name.front() == ' ') name.erase(0, 1);
            if (name.empty()) return fail(lineNumber, "an axis needs a name");
            axisNames.push_back(name);
        } else if (keyword == "checkpoint") {
            std::string tickToken;
            std::string hashToken;
            uint64_t tick = 0;
            uint64_t hash = 0;
            if (!(line >> tickToken >> hashToken) || !parseIndex(tickToken, tick) ||
                !parseHex(hashToken, 16, hash)) {
                return fail(lineNumber, "a checkpoint is a tick and sixteen hex digits");
            }
            recording.checkpoints.push_back(ReplayCheckpoint{tick, hash});
        } else if (keyword == "end") {
            std::string token;
            uint64_t declaredEnd = 0;
            if (!(line >> token) || !parseIndex(token, declaredEnd)) {
                return fail(lineNumber, "the end marker must carry the tick count");
            }
            if (!sawTickCount) return fail(lineNumber, "the header never declared a tick count");
            if (declaredEnd != declaredTicks) {
                return fail(lineNumber, "the file ends saying " + std::to_string(declaredEnd) +
                                            " ticks and began saying " +
                                            std::to_string(declaredTicks));
            }
            sawEnd = true;
        } else {
            return fail(lineNumber, "'" + keyword + "' is not part of a replay file");
        }
    }

    if (!sawMagic) return fail(lineNumber, "the file is empty");

    // THE TRUNCATION CHECK, and the reason the count is written twice.
    //
    // Delta encoding makes a half file syntactically perfect: the reader would
    // carry the last levels forward, hand back exactly the number of ticks the
    // header promised, and report success on half a session. Nothing about the
    // data can reveal that, so the file has to say where it ends.
    if (!sawEnd) {
        return fail(lineNumber, "the file stops without an end marker, so it is truncated");
    }
    if (sawAnyTick && previousTickIndex >= declaredTicks) {
        return fail(lineNumber, "tick " + std::to_string(previousTickIndex) +
                                    " is past the declared count of " +
                                    std::to_string(declaredTicks));
    }
    for (const ReplayCheckpoint& point : recording.checkpoints) {
        if (declaredTicks == 0 ? point.tick != 0 : point.tick > declaredTicks) {
            return fail(lineNumber, "a checkpoint names tick " + std::to_string(point.tick) +
                                        ", which the run never reached");
        }
    }

    // Materialise. Levels carry forward across the ticks that wrote no line;
    // edges do not, which is what stops one keypress becoming a hundred.
    recording.ticks.assign(static_cast<size_t>(declaredTicks), Input::TickInput{});

    std::vector<uint64_t> down;
    std::unordered_map<uint64_t, uint32_t> axes;
    size_t next = 0;

    for (uint64_t index = 0; index < declaredTicks; ++index) {
        const PendingTick* line = nullptr;
        if (next < pending.size() && pending[next].index == index) {
            line = &pending[next];
            down = line->down;
            axes = line->axes;
            ++next;
        }

        Input::TickInput& tick = recording.ticks[static_cast<size_t>(index)];
        tick.down = namesOf(down, actionNames);

        tick.axes.reserve(axes.size());
        for (size_t axisIdx = 0; axisIdx < axisNames.size(); ++axisIdx) {
            const auto found = axes.find(axisIdx);
            if (found == axes.end()) continue;
            tick.axes.emplace_back(axisNames[axisIdx], floatFrom(found->second));
        }

        if (line) {
            tick.pressed = namesOf(line->pressed, actionNames);
            tick.released = namesOf(line->released, actionNames);
            tick.clicked.reserve(line->clicked.size());
            for (const uint64_t id : line->clicked) {
                tick.clicked.push_back(static_cast<uint32_t>(id));
            }
            tick.mouseDelta = line->mouse;
        }
    }

    return recording;
}

const ReplayCheckpoint* InputRecording::CheckpointAt(uint64_t tick) const {
    for (const ReplayCheckpoint& point : checkpoints) {
        if (point.tick == tick) return &point;
        if (point.tick > tick) break;   // ascending, so there will not be one
    }
    return nullptr;
}

InputRecording InputRecording::Load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        InputRecording recording;
        recording.ok = false;
        recording.error = "Could not open " + path + " (nothing was replayed).";
        return recording;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return Parse(contents.str(), path);
}

bool InputRecording::Save(const InputRecording& recording, const std::string& path,
                          std::string& error) {
    // Same shape as SceneSerializer's: ofstream will not create the parent
    // directory, and a recording written to a path somebody chose is exactly
    // where that bites.
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            error = "Could not create " + target.parent_path().string() + ": " + ec.message();
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "Failed to open " + path + " for writing.";
        return false;
    }

    file << Write(recording);
    file.flush();
    if (!file) {
        error = "Write error while saving " + path + ".";
        return false;
    }
    return true;
}

} // namespace Supersonic
