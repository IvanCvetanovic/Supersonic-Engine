#pragma once

// Shared scaffolding for the ported game's suites.
//
// Two things every port suite needs and none of them should own: the shipped
// data, loaded once, and a way to author a deliberately broken copy of it.
//
// The scratch-directory class lived in two suites before this and was about to
// live in three. Two copies drift silently; three is a guarantee. It is here
// rather than in TestHarness.hpp because it is about this game's data, not
// about testing.

#include "sim/GameData.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace wb {

// The data as it ships, parsed once for the whole suite.
//
// WOLFBRIGADE_DATA_DIR is baked in at configure time: ctest runs from the build
// tree and the game's own harnesses run from the project root, so no relative
// path is right for both - and a suite that cannot find its data would validate
// ten empty documents and report a clean bill of health.
inline const WolfBrigade::GameData& Shipped() {
    static const WolfBrigade::GameData data = [] {
        WolfBrigade::GameData loaded;
        loaded.LoadAll(WOLFBRIGADE_DATA_DIR);
        return loaded;
    }();
    return data;
}

// The shipped data with one file replaced, in a scratch directory.
//
// The shipped files are correct, which is the point of them - so some
// behaviour is unreachable at shipped values and the only way to see it is to
// author it. A wave-count floor that never fires, an endless cadence nobody
// has configured: both look tested until the mutation walks straight through.
class ScratchData {
public:
    ScratchData(const std::string& tag, const std::string& file, const std::string& contents) {
        m_directory = std::filesystem::temp_directory_path() /
                      ("wb_" + tag + "_" + file + std::to_string(contents.size()));
        std::filesystem::remove_all(m_directory);
        std::filesystem::create_directories(m_directory);
        std::filesystem::copy(WOLFBRIGADE_DATA_DIR, m_directory,
                              std::filesystem::copy_options::overwrite_existing);

        std::ofstream out(m_directory / file, std::ios::binary);
        out << contents;
    }

    ~ScratchData() {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    ScratchData(const ScratchData&) = delete;
    ScratchData& operator=(const ScratchData&) = delete;

    std::string Path() const { return m_directory.string(); }

private:
    std::filesystem::path m_directory;
};

} // namespace wb
