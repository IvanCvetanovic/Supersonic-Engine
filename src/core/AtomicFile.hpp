#pragma once

#include <fstream>
#include <string>

namespace Supersonic {

// A file written so that a save which fails or is killed part-way leaves the
// PREVIOUS file, not half of the new one.
//
// A plain `std::ofstream file(path)` truncates the target the moment it opens,
// before a byte of the new content exists. A crash, a full disk or a power cut
// anywhere after that leaves a short file where a scene used to be - and for a
// .meta it is worse than a lost file: a truncated identity file fails its
// guid check, the asset is minted a NEW identity on the next import, and every
// scene that referenced the old one by guid is orphaned.
//
// So the content goes to a sibling temporary first, and replaces the target only
// once the stream is known good. The rename is atomic on the same volume, which a
// sibling always is. PipelineCache has done this since it was written; this is
// the same thing for the files a person authors.
//
//     AtomicFile file(path);
//     if (!file.IsOpen()) return failure;
//     file.Stream() << content;
//     if (!file.Commit()) return failure;
//
// A file that goes out of scope without Commit is abandoned: the temporary is
// removed and the target is untouched, which is also what an exception thrown
// part-way through writing does.
class AtomicFile {
public:
    // `binary` is the mode the caller wrote in before. A text-mode stream on
    // Windows turns "\n" into "\r\n" and a binary one does not, and a save has to
    // write the bytes it always wrote.
    explicit AtomicFile(const std::string& path, bool binary = false);
    ~AtomicFile();

    AtomicFile(const AtomicFile&) = delete;
    AtomicFile& operator=(const AtomicFile&) = delete;

    bool IsOpen() const { return m_stream.is_open(); }
    std::ostream& Stream() { return m_stream; }

    // Flushes and closes, and replaces the target only if every write succeeded.
    // Returns false, leaving the target as it was, if any of them did not.
    bool Commit();

private:
    std::string m_target;
    // Empty when the file is being written in place; see the constructor.
    std::string m_temp;
    std::ofstream m_stream;
    bool m_finished{false};
};

} // namespace Supersonic
