// Magic Portals on the Supersonic Engine: the Godot remake's levels, in the
// original's order, as the port plays them.
//
// The levels, their order, their art and the remake's data are read from
// outside this repository, from where the build was told they are
// (SUPERSONIC_MAGICPORTALS_LEVELS, _CHAPTERS and _DATA; the art beside the
// levels), or from --levels, --art and --data. Drawn with the levels' own art
// and a few lines of text. See docs/planning/2026-09-11-magic-portals-remaster.md.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/GameRuntime.hpp"
#include "core/LaunchOptions.hpp"
#include "platform/ExecutablePath.hpp"
#include "core/Log.hpp"
#include "core/SupersonicApp.hpp"
#include "renderer/VulkanContext.hpp"

#include "LevelVisit.hpp"
#include "MagicPortalsLayer.hpp"

int main(int argc, char** argv) {
    // --level, --levels, --art, --data and --saves are the game's own flags. LaunchOptions
    // refuses anything it does not know, which is right for the engine, so the
    // game takes its flags out first and hands on the rest untouched.
    MagicPortals::MagicPortalsLayer::Paths paths;
    paths.prisms = std::filesystem::temp_directory_path() / "supersonic-magicportals";
    // No --level opens the menu, which is the game's own front door. A named
    // level is entered directly, and that is how the suites and every headless
    // render run: neither ever sees the menu.
    std::string start;
    std::vector<char*> engineArgs{argv[0]};
    bool artGiven = false;
    // DEV ONLY: where the medals are kept, instead of the user's own directory.
    // It exists so a parity capture can open a level with a medal recorded - the
    // original shows its current-score plaque only then - from a scores.json
    // written for the purpose, without touching the save of whoever plays.
    std::string savesOverride;
    // DEV ONLY: walk levels in one process and check the engine's material
    // descriptor sets go back to its pool (LevelVisit.hpp). Empty = a normal run.
    MagicPortals::LevelVisitLayer::Options visit;
    // DEV ONLY: a press of the pause control, or of one of the pause's buttons, on
    // a stated tick. A --fixed-step run takes no input at all, and a parity
    // capture of the pause has to open one - and mute its sound, and resume it -
    // at a known moment. `--press pause@120` presses the pause control on the
    // layer's 120th tick.
    using DevPress = MagicPortals::MagicPortalsLayer::DevPress;
    std::vector<std::pair<int, DevPress>> presses;
    // DEV ONLY: a walk held over a stated span of ticks, as a held arrow would
    // hold it. The finished and lost screens' captures need the player walked
    // into a door or a hazard, which a --fixed-step run cannot do.
    // `--hold right@1-400` holds right from the layer's 1st tick to its 400th.
    struct Hold {
        int from = 0;
        int to = 0;
        float direction = 0.0f;
    };
    std::vector<Hold> holds;
    // DEV ONLY: a touch pressed on a stated tick and released on the next. The
    // popups' captures need a help block tapped, and a popup closed by a touch
    // down on its card, at a known moment. `--tap help@300` taps the level's first
    // help block where the camera has it; `--tap 640,200@300` taps that pixel of a
    // 1280x720 window (the spec's own coordinates), whatever the window's size.
    // `--tap 640,475@300-315` holds it from tick 300 and releases it on 315: the
    // menus' captures need a button held, to see its press tint, then released.
    struct Tap {
        int tick = 0;
        int releaseTick = 0;
        std::optional<glm::dvec2> fraction;
    };
    std::vector<Tap> taps;
    // DEV ONLY: a touch that MOVES. The credits' strip and the achievements
    // dashboard scroll with a finger, and a --fixed-step run has none.
    // `--drag 640,500:640,300@300-330` goes down at that pixel of 1280x720 on tick
    // 300, moves at an even pace, and is let go at the second on tick 330.
    struct Drag {
        int tick = 0;
        int releaseTick = 0;
        glm::dvec2 from{0.0};
        glm::dvec2 to{0.0};
    };
    std::vector<Drag> drags;
    // DEV ONLY: no 2D light reaches any sprite (MagicPortalsLayer::ForceLightMasksOff),
    // for a capture to be compared with the same capture lit.
    bool lightMasksOff = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--light-masks-off") {
            lightMasksOff = true;
            continue;
        }
        if (arg == "--drag") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            Drag drag;
            bool ok = false;
            try {
                const std::size_t at = value.find('@');
                const std::size_t colon = value.find(':');
                const std::size_t dash = at == std::string::npos ? std::string::npos : value.find('-', at);
                if (at != std::string::npos && colon != std::string::npos && colon < at && dash != std::string::npos) {
                    const auto point = [](const std::string& text, glm::dvec2& out) {
                        const std::size_t comma = text.find(',');
                        if (comma == std::string::npos) return false;
                        out = glm::dvec2(std::stod(text.substr(0, comma)) / 1280.0,
                                         std::stod(text.substr(comma + 1)) / 720.0);
                        return true;
                    };
                    drag.tick = std::stoi(value.substr(at + 1, dash - at - 1));
                    drag.releaseTick = std::stoi(value.substr(dash + 1));
                    ok = point(value.substr(0, colon), drag.from) &&
                         point(value.substr(colon + 1, at - colon - 1), drag.to);
                }
            } catch (const std::exception&) {
                ok = false;
            }
            if (!ok || drag.tick < 1 || drag.releaseTick <= drag.tick) {
                std::cerr << "--drag wants <x>,<y>:<x>,<y>@<tick>-<release tick> (pixels of 1280x720, tick at least "
                             "1, a release after it), got '"
                          << value << "'\n";
                return EXIT_FAILURE;
            }
            drags.push_back(drag);
            continue;
        }
        if (arg == "--tap") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            const std::size_t at = value.find('@');
            const std::string what = value.substr(0, at);
            Tap tap;
            bool ok = at != std::string::npos;
            try {
                if (ok) {
                    const std::string when = value.substr(at + 1);
                    const std::size_t dash = when.find('-');
                    tap.tick = std::stoi(when.substr(0, dash));
                    if (dash != std::string::npos) tap.releaseTick = std::stoi(when.substr(dash + 1));
                }
                if (ok && what != "help") {
                    const std::size_t comma = what.find(',');
                    ok = comma != std::string::npos;
                    if (ok) {
                        tap.fraction = glm::dvec2(std::stod(what.substr(0, comma)) / 1280.0,
                                                  std::stod(what.substr(comma + 1)) / 720.0);
                    }
                }
            } catch (const std::exception&) {
                ok = false;
            }
            if (!ok || tap.tick < 1 || (tap.releaseTick != 0 && tap.releaseTick <= tap.tick)) {
                std::cerr << "--tap wants help@<tick> or <x>,<y>@<tick>[-<release tick>] (pixels of 1280x720, tick "
                             "at least 1, a release after it), got '"
                          << value << "'\n";
                return EXIT_FAILURE;
            }
            taps.push_back(tap);
            continue;
        }
        if (arg == "--hold") {
            const std::string value = i + 1 < argc ? argv[++i] : "";
            const std::size_t at = value.find('@');
            const std::size_t dash = at == std::string::npos ? std::string::npos : value.find('-', at);
            const std::string what = value.substr(0, at);
            Hold hold;
            hold.direction = what == "left" ? -1.0f : what == "right" ? 1.0f : 0.0f;
            if (dash != std::string::npos) {
                try {
                    hold.from = std::stoi(value.substr(at + 1, dash - at - 1));
                    hold.to = std::stoi(value.substr(dash + 1));
                } catch (const std::exception&) {
                    hold.from = 0;
                }
            }
            if (hold.direction == 0.0f || hold.from < 1 || hold.to < hold.from) {
                std::cerr << "--hold wants <left|right>@<first tick>-<last tick>, from at least 1, got '" << value
                          << "'\n";
                return EXIT_FAILURE;
            }
            holds.push_back(hold);
            continue;
        }
        if (arg == "--press") {
            if (i + 1 >= argc) {
                std::cerr << "--press needs a value, e.g. pause@120\n";
                return EXIT_FAILURE;
            }
            const std::string value = argv[++i];
            const std::size_t at = value.find('@');
            const std::string what = value.substr(0, at);
            const struct {
                const char* name;
                DevPress press;
            } names[] = {{"pause", DevPress::Pause},
                         {"levels", DevPress::Levels},
                         {"resume", DevPress::Resume},
                         {"skip", DevPress::Skip},
                         {"achievements", DevPress::Achievements},
                         {"sound", DevPress::Sound},
                         {"music", DevPress::Music},
                         {"back", DevPress::Back}};
            int tick = 0;
            if (at != std::string::npos) {
                try {
                    tick = std::stoi(value.substr(at + 1));
                } catch (const std::exception&) {
                    tick = 0;
                }
            }
            bool known = false;
            for (const auto& name : names) {
                if (what != name.name) continue;
                known = true;
                if (tick >= 1) presses.emplace_back(tick, name.press);
            }
            if (!known || tick < 1) {
                std::cerr << "--press wants <pause|levels|resume|skip|achievements|sound|music|back>@<tick of at least "
                             "1>,"
                             " got '"
                          << value << "'\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (arg == "--visit-levels" || arg == "--visit-passes") {
            if (i + 1 >= argc) {
                std::cerr << arg << " needs a value\n";
                return EXIT_FAILURE;
            }
            const std::string value = argv[++i];
            if (arg == "--visit-levels") {
                visit.levels = value;
            } else {
                try {
                    visit.passes = std::stoi(value);
                } catch (const std::exception&) {
                    visit.passes = 0;
                }
                if (visit.passes < 1) {
                    std::cerr << "--visit-passes wants a whole number of at least 1, got '" << value << "'\n";
                    return EXIT_FAILURE;
                }
            }
            continue;
        }
        if (arg == "--level" || arg == "--levels" || arg == "--art" || arg == "--data" || arg == "--saves") {
            if (i + 1 >= argc) {
                std::cerr << arg << " needs a value\n";
                return EXIT_FAILURE;
            }
            const std::string value = argv[++i];
            if (arg == "--level") {
                start = value;
            } else if (arg == "--levels") {
                paths.levels = value;
            } else if (arg == "--art") {
                paths.art = value;
                artGiven = true;
            } else if (arg == "--saves") {
                savesOverride = value;
            } else {
                paths.data = value;
            }
            continue;
        }
        engineArgs.push_back(argv[i]);
    }
    // The converter writes the art beside the levels, so levels somewhere else
    // bring their art with them unless --art says otherwise.
    if (!artGiven && paths.levels != MAGICPORTALS_LEVELS_DIR) paths.art = paths.levels + "/..";

    const auto options = Supersonic::LaunchOptions::Parse(static_cast<int>(engineArgs.size()), engineArgs.data());
    if (options.helpRequested) {
        std::cout << Supersonic::LaunchOptions::Usage()
                  << "  --level <name>    the level to start at, as chapters.json names it. Without it\n"
                  << "                    the game opens its menu.\n"
                  << "  --levels <dir>    the converted levels (default " << paths.levels << ")\n"
                  << "  --art <dir>       what their res:// stands for (default the directory above them)\n"
                  << "  --data <dir>      the remake's game/data directory (default " << paths.data << ")\n"
                  << "  --saves <dir>     DEV: keep the medals (scores.json) here instead of the user's\n"
                  << "                    data directory - for a capture that needs a medal recorded\n"
                  << "  --press <what>@<tick>\n"
                  << "                    DEV: press the pause control (pause), one of the pause's\n"
                  << "                    buttons (levels, resume, skip, achievements, sound, music) or\n"
                  << "                    the back key on a menu state (back) on the game's tick <tick>,\n"
                  << "                    for a capture; repeatable\n"
                  << "  --tap <help|x,y>@<tick>[-<release>]\n"
                  << "                    DEV: a touch down on the game's tick <tick> and up on the next\n"
                  << "                    (or held there until <release>), on the level's first help block\n"
                  << "                    or at pixel x,y of 1280x720, for a capture that opens or closes a\n"
                  << "                    popup or presses a menu's button; repeatable\n"
                  << "  --drag <x,y>:<x,y>@<tick>-<release>\n"
                  << "                    DEV: a touch down at the first pixel of 1280x720 on <tick>,\n"
                  << "                    moved evenly to the second and let go there on <release>, for a\n"
                  << "                    capture that scrolls the credits or the achievements; repeatable\n"
                  << "  --hold <left|right>@<from>-<to>\n"
                  << "                    DEV: hold a walk from the game's tick <from> to <to>, for a\n"
                  << "                    capture that walks into a door or a hazard; repeatable\n"
                  << "  --visit-levels <lightmapped|all|name,...>\n"
                  << "                    DEV: visit these levels in one process, taking the material set\n"
                  << "                    of every lightmapped sprite, which the level drops on leaving; the\n"
                  << "                    run fails unless the engine's descriptor sets return to its pool\n"
                  << "  --visit-passes <n> DEV: how many times to walk that list (default 1)\n"
                  << "  --light-masks-off DEV: draw the lights and halos but let no light reach a sprite,\n"
                  << "                    for a capture to be compared with the same capture lit\n";
        return EXIT_SUCCESS;
    }
    if (!options.ok) {
        std::cerr << options.error << "\n";
        return EXIT_FAILURE;
    }

    // The diagnostics to a FILE as well as the console.
    //
    // Nothing else in the engine opens this sink, and Log.hpp says why it
    // matters: a shipped game gets WIN32_EXECUTABLE and has no console at all,
    // so without this its diagnostics go nowhere. That is no use at all when
    // the thing being chased only happens while somebody else is playing.
    const std::filesystem::path logPath = std::filesystem::temp_directory_path() / "magicportals.log";
    if (Supersonic::Log::SetFileSink(logPath.string())) {
        std::cout << "[Magic Portals] logging to " << logPath.string() << std::endl;
    }

    // Declared, not discovered, for the reason Wolf Brigade's main gives: a
    // game binary is a game whether or not a manifest sits beside it.
    Supersonic::GameManifest manifest;
    manifest.isGame = true;
    manifest.title = "Magic Portals";
    // No startup scene: the layer builds the level. Left at the manifest's
    // default, a run from the repository root opens the editor's demo scene
    // beside it, which the first headless render showed above level30.
    manifest.startupScene.clear();

    // WHERE THIS GAME MAY WRITE, resolved once, here, and handed down.
    //
    // The layer does not ask for itself, which is WolfBrigadeLayer's contract
    // and exists for its reason: "where may I write" is a question about the
    // machine rather than about this game, and a layer that resolved its own
    // path could not be built by a test without one. Eighteen suites build this
    // layer bare, and every one of them would otherwise be writing the same
    // scores.json into the ctest working directory and reading each other's.
    //
    // Empty when the platform will not say: the medals are then not kept, which
    // the game says out loud rather than writing somewhere unpredictable.
    const std::filesystem::path saveDir =
        savesOverride.empty() ? Supersonic::UserDataDirectory(manifest.title) : std::filesystem::path(savesOverride);
    if (saveDir.empty()) {
        std::cerr << "[Magic Portals] no writable user directory; "
                     "medals earned this session will not be kept.\n";
    }
    paths.saveDir = saveDir.string();

    // Outside the app, because the layer stack is torn down with it.
    MagicPortals::LevelVisitLayer::Result visitResult;
    try {
        Supersonic::SupersonicApp app(options, &manifest);

        // SAID OUT LOUD, because the two cases are indistinguishable otherwise.
        //
        // A run with the validation layers absent produces exactly the output a
        // clean run does: none. This game was played through a Release build,
        // where SUPERSONIC_ENABLE_VALIDATION defaults to 0, and its silent log
        // was nearly read as evidence that the frames were clean - when in fact
        // nothing had been watching. VulkanContext.hpp makes the same point
        // about CI: "a green run that never loaded the layer proves nothing and
        // looks identical to one that did".
        SUPERSONIC_LOG_INFO("Magic Portals")
            << "Vulkan validation layers: "
            << (Supersonic::VulkanContext::ValidationLayersActive() ? "ACTIVE"
                                                                    : "NOT LOADED - nothing is checking this run")
            << std::endl;

        auto game = std::make_unique<MagicPortals::MagicPortalsLayer>(paths, start);
        for (const auto& [tick, press] : presses) game->ScheduleDevPress(tick, press);
        for (const Hold& hold : holds) game->ScheduleDevHold(hold.from, hold.to, hold.direction);
        for (const Tap& tap : taps) game->ScheduleDevTap(tap.tick, tap.fraction, tap.releaseTick);
        for (const Drag& drag : drags) game->ScheduleDevDrag(drag.tick, drag.from, drag.to, drag.releaseTick);
        MagicPortals::MagicPortalsLayer& gameLayer = *game;
        gameLayer.ForceLightMasksOff(lightMasksOff);
        app.PushLayer(std::move(game));
        if (!visit.levels.empty()) {
            app.PushLayer(std::make_unique<MagicPortals::LevelVisitLayer>(gameLayer, paths, visit, visitResult));
        }
        app.Run();
    } catch (const std::exception& e) {
        std::cerr << "[Magic Portals] fatal: " << e.what() << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    if (!visit.levels.empty() && (!visitResult.finished || !visitResult.failures.empty())) {
        std::cerr << "[Magic Portals] level visit "
                  << (visitResult.finished ? "failed" : "did not finish") << ": "
                  << visitResult.failures.size() << " failure(s)";
        if (!visitResult.failures.empty()) std::cerr << ", the first: " << visitResult.failures.front();
        std::cerr << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    const unsigned validationErrors = Supersonic::VulkanContext::ValidationErrorCount();
    if (validationErrors > 0) {
        std::cerr << "[Magic Portals] " << validationErrors << " Vulkan validation error(s); failing the run."
                  << std::endl;
        Supersonic::Log::CloseFileSink();
        return EXIT_FAILURE;
    }

    Supersonic::Log::CloseFileSink();
    return EXIT_SUCCESS;
}
