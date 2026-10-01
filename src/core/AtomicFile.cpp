#include "core/AtomicFile.hpp"

#include <filesystem>
#include <system_error>

namespace Supersonic {

namespace fs = std::filesystem;

AtomicFile::AtomicFile(const std::string& path, bool binary) : m_target(path) {
    const std::ios::openmode mode =
        std::ios::out | std::ios::trunc | (binary ? std::ios::binary : std::ios::openmode{});

    // A symlink is written THROUGH: the temporary is made beside the file it
    // points at and replaces that, rather than the rename replacing the link with
    // a regular file and leaving what it pointed at behind.
    std::error_code ec;
    if (fs::is_symlink(m_target, ec)) {
        const fs::path resolved = fs::canonical(m_target, ec);
        if (!ec) m_target = resolved.string();
    }

    m_temp = m_target + ".tmp";
    m_stream.open(m_temp, mode);
    if (m_stream.is_open()) return;

    // The temporary could not be made - a directory that cannot be written to,
    // beside a file that can. That is a case the old in-place write handled, and
    // refusing to save where it used to work would be a regression, so fall back
    // to exactly what it did. Atomicity is lost for that save and nothing else.
    m_temp.clear();
    m_stream.clear();
    m_stream.open(m_target, mode);
}

AtomicFile::~AtomicFile() {
    if (m_finished) return;

    // Abandoned: never committed, or an exception passed through. Whatever was
    // written to the temporary is not wanted, and the target was never touched.
    m_stream.close();
    if (!m_temp.empty()) {
        std::error_code ec;
        fs::remove(m_temp, ec);
    }
}

bool AtomicFile::Commit() {
    if (m_finished) return false;
    m_finished = true;

    m_stream.flush();
    const bool wroteEverything = static_cast<bool>(m_stream);
    m_stream.close();
    const bool closedCleanly = !m_stream.fail();

    // Written in place: nothing to rename, and nothing to preserve either.
    if (m_temp.empty()) return wroteEverything && closedCleanly;

    std::error_code ec;
    if (!wroteEverything || !closedCleanly) {
        fs::remove(m_temp, ec);
        return false;
    }

    // The target's permissions, so a save does not quietly reset a file somebody
    // made group-writable. Only when there is a target; a failure here is not a
    // reason to lose the save.
    if (fs::exists(m_target, ec)) {
        const fs::file_status existing = fs::status(m_target, ec);
        if (!ec) fs::permissions(m_temp, existing.permissions(), ec);
    }

    ec.clear();
    fs::rename(m_temp, m_target, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(m_temp, ignored);
        return false;
    }
    return true;
}

} // namespace Supersonic
