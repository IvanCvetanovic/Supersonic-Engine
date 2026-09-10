// Where a game writes what belongs to the player.
//
// The engine had no answer to this at all, which is why the one game in the
// tree constructed a Profile it never saved and handed its Match an empty run
// path. Both obvious answers are wrong in a way that only shows up after
// shipping: beside the executable fails on a read-only install, and the working
// directory is whatever the shortcut pointed at.
//
// Two things are checked here and they need different instruments. The
// SANITISER is pure, so it is checked directly and exhaustively. The DIRECTORY
// touches the real environment and the real filesystem, so it is checked
// through properties that hold whatever this machine's %APPDATA% happens to be
// - it is absolute, it exists afterwards, it is under the platform's own root,
// and two names do not collide - rather than against a path written down here,
// which would pass only on the machine that wrote it.

#include "TestHarness.hpp"

#include "platform/ExecutablePath.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Supersonic;

namespace {

#if defined(_WIN32)
// Read wide, the way UserDataDirectory reads it, so the comparisons below
// cannot disagree with it about a name the ANSI code page cannot spell.
std::filesystem::path environmentPath(const wchar_t* name) {
    wchar_t* raw = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&raw, &length, name) != 0 || raw == nullptr) return {};
    std::filesystem::path value(raw);
    std::free(raw);
    return value;
}
#endif

// --- The sanitiser -------------------------------------------------------

void testAnOrdinaryNameIsLeftAlone() {
    CHECK(SanitiseForPathComponent("Wolf Brigade") == "Wolf Brigade");
    CHECK(SanitiseForPathComponent("HUSK") == "HUSK");
    CHECK(SanitiseForPathComponent("game-2_v3") == "game-2_v3");
}

void testASeparatorCannotEscapeTheFolderItNames() {
    // The whole reason the name is sanitised rather than trusted. `title` comes
    // out of a manifest a game author wrote, and a path component is not a
    // place to discover that they used a slash.
    CHECK(SanitiseForPathComponent("a/b") == "a_b");
    CHECK(SanitiseForPathComponent("a\\b") == "a_b");
    CHECK(SanitiseForPathComponent("..") == "");
    CHECK(SanitiseForPathComponent("../../etc") == "____etc");
    CHECK(SanitiseForPathComponent("C:name") == "C_name");
}

void testTrailingDotsAndSpacesAreStrippedRatherThanReplaced() {
    // Windows drops a trailing dot from a directory name silently, so a folder
    // CREATED as "Game." is later OPENED as "Game" and the two names stop
    // agreeing about where the save is. Replacing it with an underscore would
    // keep them agreeing but on a name nobody chose.
    CHECK(SanitiseForPathComponent("Game.") == "Game");
    CHECK(SanitiseForPathComponent("Game...") == "Game");
    CHECK(SanitiseForPathComponent("  Game  ") == "Game");
    CHECK(SanitiseForPathComponent(" . Game . ") == "Game");
}

void testANameWithNothingUsableInItIsRefused() {
    // Empty rather than a made-up default. A game whose title sanitises to
    // nothing has a bug in its manifest, and inventing "Supersonic" for it
    // would put two such games in the same folder.
    CHECK(SanitiseForPathComponent("") == "");
    CHECK(SanitiseForPathComponent("...") == "");
    CHECK(SanitiseForPathComponent("   ") == "");
}

void testTheDotInAVersionSurvivesInTheMiddle() {
    // Only the padding is stripped. A dot between two kept characters is a
    // legitimate part of a name and there is no reason to lose it.
    CHECK(SanitiseForPathComponent("Game v1.2") == "Game v1_2");
}

// --- The directory -------------------------------------------------------

void testTheDirectoryIsAbsoluteAndExistsAfterwards() {
    const std::filesystem::path directory = UserDataDirectory("Supersonic Test Suite");

    CHECK_MSG(!directory.empty(), "this platform must be able to say where home is");
    if (directory.empty()) return;

    CHECK_MSG(directory.is_absolute(), "a save path a shortcut can move is not a save path");

    std::error_code ec;
    CHECK_MSG(std::filesystem::is_directory(directory, ec),
              "it is CREATED, because every caller would otherwise have to");

    // And it is genuinely writable, which is the only property the caller
    // actually wants. A directory that exists and refuses writes is the exact
    // failure that ruled out installing beside the executable.
    const std::filesystem::path probe = directory / "write_probe.txt";
    {
        std::ofstream out(probe, std::ios::binary);
        out << "ok";
    }
    CHECK_MSG(std::filesystem::exists(probe, ec), "and written into");

    std::filesystem::remove(probe, ec);
    std::filesystem::remove(directory, ec);
}

void testItSitsUnderThePlatformsOwnRootRatherThanBesideTheExecutable() {
    const std::filesystem::path directory = UserDataDirectory("Supersonic Test Suite");
    if (directory.empty()) return;

    // Compared against the environment this machine actually has, not against a
    // path written down here - a literal would pass only where it was written.
#if defined(_WIN32)
    const std::filesystem::path root = environmentPath(L"APPDATA");
#else
    const char* home = std::getenv("HOME");
    const std::filesystem::path root = home ? std::filesystem::path(home) : std::filesystem::path();
#endif
    CHECK_MSG(!root.empty(), "the platform names a root");
    if (root.empty()) return;

    const std::string prefix = root.string();
    CHECK_MSG(directory.string().rfind(prefix, 0) == 0,
              "the save goes under the user's own root: got " + directory.string());

    // And explicitly NOT beside the binary, which is the answer this function
    // exists to replace.
    CHECK_MSG(directory.parent_path() != ExecutableDirectory(),
              "not beside the executable, which a read-only install forbids");

    std::error_code ec;
    std::filesystem::remove(directory, ec);
}

void testTwoGamesDoNotShareASaveFolder() {
    const std::filesystem::path first = UserDataDirectory("Supersonic Test One");
    const std::filesystem::path second = UserDataDirectory("Supersonic Test Two");
    if (first.empty() || second.empty()) return;

    CHECK_MSG(first != second, "each game gets its own folder");

    std::error_code ec;
    std::filesystem::remove(first, ec);
    std::filesystem::remove(second, ec);
}

void testASeparatorInTheNameDoesNotNestTheSaveFolder() {
    // The directory-side half of the sanitiser check, and it is here because
    // the pure tests above cannot reach it: they call SanitiseForPathComponent
    // themselves, so a UserDataDirectory that stopped calling it would leave
    // every one of them green. A name carrying a separator would then create a
    // NESTED directory that exists and is writable, which passes every other
    // property in this file.
    const std::filesystem::path nested = UserDataDirectory("Supersonic/Test/Nested");
    if (nested.empty()) return;

    CHECK_MSG(nested.filename() == "Supersonic_Test_Nested",
              "the whole name is one component: got " + nested.filename().string());

    const std::filesystem::path plain = UserDataDirectory("Supersonic Test Suite");
    CHECK_MSG(nested.parent_path() == plain.parent_path(),
              "and it sits beside an ordinary game rather than under one");

    std::error_code ec;
    std::filesystem::remove(nested, ec);
    std::filesystem::remove(plain, ec);
}

void testAProfileFolderOutsideTheCodePageIsFoundByItsRealName() {
#if defined(_WIN32)
    // The narrow environment is the wide one pushed through the ANSI code
    // page, so on a Western European system a c-acute comes back from getenv
    // as a plain c and a CJK character as '?' - and the save goes to a folder
    // that is not the player's, or to none. Pointing APPDATA at such a folder
    // for one call is the only way to see it: the real profile on a build
    // machine is almost always plain ASCII, where the narrow and wide reads
    // agree. Built from code points because the build names no source charset,
    // and MSVC would read a raw UTF-8 literal through the code page under test.
    const std::filesystem::path original = environmentPath(L"APPDATA");
    std::error_code ec;
    const std::wstring folder =
        std::wstring(L"Supersonic Cvetanovi") + wchar_t{0x0107} + L' ' + wchar_t{0x96EA};
    const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / folder;
    std::filesystem::create_directories(root, ec);
    CHECK_MSG(!ec, "the stand-in profile folder can be made");
    if (ec) return;

    _wputenv_s(L"APPDATA", root.c_str());
    const std::filesystem::path directory = UserDataDirectory("Supersonic Test Suite");
    _wputenv_s(L"APPDATA", original.c_str());

    CHECK_MSG(directory.parent_path() == root,
              "the save goes under the folder the profile is really called");
    CHECK_MSG(std::filesystem::is_directory(directory, ec), "and it is really there");

    std::filesystem::remove_all(root, ec);
#endif
}

void testAGameWithNoUsableNameGetsNoDirectory() {
    // It must not fall back to the root itself. Creating %APPDATA% and handing
    // it back would put one game's save beside every other application's data.
    CHECK_MSG(UserDataDirectory("").empty(), "an empty name gets nothing");
    CHECK_MSG(UserDataDirectory("...").empty(), "and so does one that sanitises away");
}

void testTheSameNameGivesTheSamePlaceTwice() {
    // A save the game cannot find again is not a save. This is the property the
    // working-directory answer fails.
    const std::filesystem::path first = UserDataDirectory("Supersonic Test Suite");
    const std::filesystem::path second = UserDataDirectory("Supersonic Test Suite");
    CHECK_MSG(first == second, "asking twice gives one answer");

    std::error_code ec;
    std::filesystem::remove(first, ec);
}

} // namespace

static void runTests() {
    testAnOrdinaryNameIsLeftAlone();
    testASeparatorCannotEscapeTheFolderItNames();
    testTrailingDotsAndSpacesAreStrippedRatherThanReplaced();
    testANameWithNothingUsableInItIsRefused();
    testTheDotInAVersionSurvivesInTheMiddle();

    testTheDirectoryIsAbsoluteAndExistsAfterwards();
    testItSitsUnderThePlatformsOwnRootRatherThanBesideTheExecutable();
    testTwoGamesDoNotShareASaveFolder();
    testASeparatorInTheNameDoesNotNestTheSaveFolder();
    testAProfileFolderOutsideTheCodePageIsFoundByItsRealName();
    testAGameWithNoUsableNameGetsNoDirectory();
    testTheSameNameGivesTheSamePlaceTwice();
}

TEST_MAIN("test_userdata", 29)
