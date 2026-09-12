#include "sim/Particles.hpp"

#include <cctype>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>

namespace MagicPortals::Particles {

namespace {

// The .ent files are UTF-16 LE with a byte-order mark, every one of the 190.
// Their content is ASCII throughout - tag names, numbers and file names - so
// the low byte of each unit is the character. A unit with a high byte is
// something this reader was not written for, and is refused rather than
// mangled into a character nobody typed.
bool ReadUtf16(const std::string& path, std::string& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = path + ": cannot be read";
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
    if (bytes.size() < 2 || bytes[0] != 0xFF || bytes[1] != 0xFE) {
        error = path + ": is not UTF-16 LE, which every .ent of the original's is";
        return false;
    }
    if (bytes.size() % 2 != 0) {
        error = path + ": ends in half a UTF-16 unit";
        return false;
    }
    out.clear();
    out.reserve(bytes.size() / 2);
    for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
        if (bytes[i + 1] != 0) {
            error = path + ": holds a character outside ASCII, which this reader does not carry";
            return false;
        }
        out.push_back(static_cast<char>(bytes[i]));
    }
    return true;
}

// The text between `open` and `close`, or false when either is absent.
bool Between(const std::string& text, const std::string& open, const std::string& close, std::string& out) {
    const std::size_t from = text.find(open);
    if (from == std::string::npos) return false;
    const std::size_t start = from + open.size();
    const std::size_t to = text.find(close, start);
    if (to == std::string::npos) return false;
    out = text.substr(start, to - start);
    return true;
}

// `name="..."` out of an opening tag's attributes.
bool Attribute(const std::string& tag, const std::string& name, std::string& out) {
    const std::string key = name + "=\"";
    std::size_t at = tag.find(key);
    // A name that ends another's - `size` inside `randomizeSize` - is not this
    // one. Take only a match whose character before is a space.
    while (at != std::string::npos && at != 0 && !std::isspace(static_cast<unsigned char>(tag[at - 1]))) {
        at = tag.find(key, at + 1);
    }
    if (at == std::string::npos) return false;
    const std::size_t start = at + key.size();
    const std::size_t end = tag.find('"', start);
    if (end == std::string::npos) return false;
    out = tag.substr(start, end - start);
    return true;
}

bool Number(const std::string& text, double& out) {
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used == 0) return false;
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// One <ParticleSystem>...</ParticleSystem>, parsed out of its OWN chunk of the
// file. Per chunk, and not per <Particles> block, because 21 entities carry two
// systems and the children of the first would otherwise answer for both.
bool ParseSystem(const std::string& chunk, System& out, std::string& missing) {
    std::string tag;
    if (!Between(chunk, "<ParticleSystem", ">", tag)) {
        missing = "an opening tag";
        return false;
    }

    System read;
    bool ok = true;
    const auto number = [&](const char* name, double& to) {
        std::string value;
        double parsed = 0.0;
        if (!Attribute(tag, name, value) || !Number(value, parsed)) {
            if (ok) missing = name;
            ok = false;
            return;
        }
        to = parsed;
    };

    // Every one of these is in all 102 systems the original ships, so a system
    // without one is a system this reader has misread - not a default to invent.
    double count = 0.0;
    double allAtOnce = 0.0;
    double alphaMode = 0.0;
    double animationMode = 0.0;
    double repeat = 0.0;
    number("particles", count);
    number("allAtOnce", allAtOnce);
    number("alphaMode", alphaMode);
    number("animationMode", animationMode);
    number("repeat", repeat);
    number("lifeTime", read.lifeTimeMs);
    number("randomLifeTime", read.randomLifeTimeMs);
    number("size", read.size);
    number("randomizeSize", read.randomizeSize);
    number("growth", read.growth);
    number("minSize", read.minSize);
    number("maxSize", read.maxSize);
    number("angleStart", read.angleStart);
    number("randAngleStart", read.randAngleStart);
    number("angleDir", read.angleDir);
    number("randAngle", read.randAngle);

    read.count = static_cast<int>(count);
    read.allAtOnce = allAtOnce != 0.0;
    read.additive = static_cast<int>(alphaMode) == 1; // AM_ADD
    read.animationMode = static_cast<int>(animationMode);
    read.repeat = static_cast<int>(repeat);

    const auto vector2 = [&](const char* name, glm::dvec2& to) {
        std::string element;
        std::string x;
        std::string y;
        double px = 0.0;
        double py = 0.0;
        if (!Between(chunk, std::string("<") + name, "/>", element) || !Attribute(element, "x", x) ||
            !Attribute(element, "y", y) || !Number(x, px) || !Number(y, py)) {
            if (ok) missing = name;
            ok = false;
            return;
        }
        to = glm::dvec2(px, py);
    };
    const auto colour = [&](const char* name, glm::dvec4& to) {
        std::string element;
        std::string r;
        std::string g;
        std::string b;
        std::string a;
        double pr = 0.0;
        double pg = 0.0;
        double pb = 0.0;
        double pa = 0.0;
        if (!Between(chunk, std::string("<") + name, "/>", element) || !Attribute(element, "r", r) ||
            !Attribute(element, "g", g) || !Attribute(element, "b", b) || !Attribute(element, "a", a) ||
            !Number(r, pr) || !Number(g, pg) || !Number(b, pb) || !Number(a, pa)) {
            if (ok) missing = name;
            ok = false;
            return;
        }
        to = glm::dvec4(pr, pg, pb, pa);
    };

    vector2("Gravity", read.gravity);
    vector2("Direction", read.direction);
    vector2("RandomizeDir", read.randomizeDir);
    vector2("StartPoint", read.startPoint);
    vector2("RandStartPoint", read.randStartPoint);
    colour("Color0", read.colour0);
    colour("Color1", read.colour1);

    glm::dvec2 cut(1.0);
    vector2("SpriteCut", cut);
    read.columns = static_cast<int>(cut.x);
    read.rows = static_cast<int>(cut.y);

    if (!Between(chunk, "<Bitmap>", "</Bitmap>", read.bitmap)) {
        if (ok) missing = "Bitmap";
        ok = false;
    }

    if (!ok) return false;
    out = read;
    return true;
}

} // namespace

bool Load(const std::string& path, std::vector<System>& out, std::string& error) {
    out.clear();
    std::string text;
    if (!ReadUtf16(path, text, error)) return false;

    std::string particles;
    if (!Between(text, "<Particles>", "</Particles>", particles)) return true; // none: not an error

    std::size_t at = 0;
    while (true) {
        const std::size_t start = particles.find("<ParticleSystem", at);
        if (start == std::string::npos) break;
        const std::size_t close = particles.find("</ParticleSystem>", start);
        if (close == std::string::npos) {
            error = path + ": a particle system is never closed";
            out.clear();
            return false;
        }
        System system;
        std::string missing;
        if (!ParseSystem(particles.substr(start, close - start), system, missing)) {
            error = path + ": a particle system has no " + missing;
            out.clear();
            return false;
        }
        if (system.count <= 0) {
            error = path + ": a particle system holds no particles";
            out.clear();
            return false;
        }
        if (system.columns <= 0 || system.rows <= 0) {
            error = path + ": a particle system's SpriteCut is not a grid";
            out.clear();
            return false;
        }
        out.push_back(system);
        at = close + 1;
    }
    return true;
}

} // namespace MagicPortals::Particles
