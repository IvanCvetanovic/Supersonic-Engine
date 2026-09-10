#include "sim/Prism.hpp"

#include "sim/Units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace MagicPortals::Prism {

namespace {

struct Point {
    double x;
    double y;
};

double SignedArea(const std::vector<Point>& points) {
    double twice = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return 0.5 * twice;
}

// FNV-1a, 64 bits: a file name that is a function of the file's contents.
std::uint64_t Fnv1a(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

void AppendVertex(std::string& out, double x, double y, double z) {
    char line[96];
    std::snprintf(line, sizeof line, "v %.9g %.9g %.9g\n", x, y, z);
    out += line;
}

// OBJ indices are one-based.
void AppendFace(std::string& out, std::size_t a, std::size_t b, std::size_t c) {
    char line[64];
    std::snprintf(line, sizeof line, "f %zu %zu %zu\n", a, b, c);
    out += line;
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

double AreaPx(const std::vector<double>& polygonPx) {
    const std::size_t n = polygonPx.size() / 2;
    double twice = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        twice += polygonPx[2 * i] * polygonPx[2 * j + 1] - polygonPx[2 * j] * polygonPx[2 * i + 1];
    }
    return std::fabs(0.5 * twice);
}

std::string ObjText(const std::vector<double>& polygonPx, double depthMetres, std::string& error) {
    if (polygonPx.size() % 2 != 0 || polygonPx.size() < 6) {
        error = "a prism needs a polygon of at least three points";
        return {};
    }
    if (!(depthMetres > 0.0)) {
        error = "a prism needs a positive depth";
        return {};
    }

    std::vector<Point> points;
    for (std::size_t i = 0; i + 1 < polygonPx.size(); i += 2)
        points.push_back({polygonPx[i] / Units::kPixelsPerMetre, -polygonPx[i + 1] / Units::kPixelsPerMetre});

    // Counter-clockwise about +z, so the caps and sides below all face out.
    // The y flip alone reverses whatever order the converter wrote.
    if (SignedArea(points) < 0.0) std::reverse(points.begin(), points.end());
    if (!(SignedArea(points) > 0.0)) {
        error = "the polygon has no area";
        return {};
    }

    // Convex: no turn to the right. A reflex corner would be filled in by the
    // hull; a straight one is only a point the hull drops, which is harmless.
    const std::size_t n = points.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % n];
        const Point& c = points[(i + 2) % n];
        const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (cross < -1e-12) {
            error = "the polygon is not convex at point " + std::to_string((i + 1) % n);
            return {};
        }
    }

    const double h = depthMetres * 0.5;
    char header[96];
    std::snprintf(header, sizeof header, "# Magic Portals collision prism: %zu points, %.9g m deep\n", n, depthMetres);
    std::string obj = header;

    for (const Point& p : points) AppendVertex(obj, p.x, p.y, h);  // front, 1..n
    for (const Point& p : points) AppendVertex(obj, p.x, p.y, -h); // back, n+1..2n

    // The front cap faces +z: counter-clockwise from there, fanned from the
    // first point, which a convex polygon allows. The back cap is the same fan
    // reversed.
    for (std::size_t i = 1; i + 1 < n; ++i) AppendFace(obj, 1, 1 + i, 2 + i);
    for (std::size_t i = 1; i + 1 < n; ++i) AppendFace(obj, n + 1, n + 2 + i, n + 1 + i);

    // Each side is the quad over edge i -> j, front and back, split in two with
    // the outward normal: to the right of the edge, for a counter-clockwise ring.
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        AppendFace(obj, 1 + i, n + 1 + j, 1 + j);
        AppendFace(obj, 1 + i, n + 1 + i, n + 1 + j);
    }
    return obj;
}

std::string Write(const std::vector<double>& polygonPx, double depthMetres,
                  const std::filesystem::path& directory, std::string& error) {
    const std::string text = ObjText(polygonPx, depthMetres, error);
    if (text.empty()) return {};

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "cannot create " + directory.string() + ": " + ec.message();
        return {};
    }

    char name[40];
    std::snprintf(name, sizeof name, "prism-%016llx.obj", static_cast<unsigned long long>(Fnv1a(text)));
    const std::filesystem::path path = directory / name;

    // The name is the contents, so a file already there that reads the same IS
    // this prism, from an earlier run. Anything else - missing, or cut short by
    // a run that died writing it - is rewritten beside it and moved into place,
    // so a reader never sees half a file.
    if (ReadAll(path) != text) {
        const std::filesystem::path partial = path.string() + ".partial";
        {
            std::ofstream file(partial, std::ios::binary | std::ios::trunc);
            if (!(file << text)) {
                error = "cannot write " + partial.string();
                return {};
            }
        }
        std::filesystem::rename(partial, path, ec);
        if (ec) {
            error = "cannot move " + partial.string() + " into place: " + ec.message();
            return {};
        }
    }
    return path.string();
}

} // namespace MagicPortals::Prism
