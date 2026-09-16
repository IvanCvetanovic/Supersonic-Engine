#include "sim/Particles.hpp"

#include "sim/Units.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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
    read.alphaMode = static_cast<int>(alphaMode);
    read.additive = read.alphaMode == kAlphaAdd;
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
    // Three channels and no alpha: the tenth child, which all 102 systems state.
    {
        std::string element;
        std::string r;
        std::string g;
        std::string b;
        double pr = 0.0;
        double pg = 0.0;
        double pb = 0.0;
        if (!Between(chunk, "<Luminance", "/>", element) || !Attribute(element, "r", r) ||
            !Attribute(element, "g", g) || !Attribute(element, "b", b) || !Number(r, pr) || !Number(g, pg) ||
            !Number(b, pb)) {
            if (ok) missing = "Luminance";
            ok = false;
        } else {
            read.luminance = glm::dvec3(pr, pg, pb);
        }
    }

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

bool Drawable(const System& system) {
    return system.alphaMode == kAlphaAdd || system.alphaMode == kAlphaPixel;
}

void Scale(System& system, double scale) {
    // ETHParticleSystem.cpp:27-40, field by field, in its order.
    system.gravity *= scale;
    system.direction *= scale;
    system.randomizeDir *= scale;
    system.startPoint *= scale;
    system.randStartPoint *= scale;
    system.size *= scale;
    system.randomizeSize *= scale;
    system.growth *= scale;
    system.minSize *= scale;
    system.maxSize *= scale;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

// UpdateParticleSystem's frame-speed unit is a sixtieth of a second, and the frame
// is capped at 250 ms before it becomes one, so a stall does not fling every
// particle across the level (ETHParticleManager.cpp:195-196).
constexpr double kFrameCapMs = 250.0;
constexpr double kFrameSpeedPerMs = 60.0 / 1000.0;

} // namespace

glm::dvec2 Turn(const glm::dvec2& px, double angleDeg) {
    // At 0 the cosine is exactly 1 and the sine exactly 0, so an unturned owner's
    // arithmetic is exact too.
    const double radians = angleDeg * kPi / 180.0;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return glm::dvec2(px.x * c + px.y * s, -px.x * s + px.y * c);
}

void Release(const System& system, Particle& particle, const Owner& owner, const Random& random) {
    // Randomizer::Float(max) is 0..max, which random(0, max) is on either side of 0.
    particle.angleDeg = system.angleStart + random(0.0, system.randAngleStart) + owner.angleDeg;
    const glm::dvec2 half = system.randStartPoint / 2.0;
    // x draws before y, in two statements as the original's are (:518-519): the
    // two arguments of one constructor are evaluated in an order C++ leaves open,
    // and a compiler's choice would reorder the generator's sequence.
    const double startX = system.startPoint.x + random(-half.x, half.x);
    const double startY = system.startPoint.y + random(-half.y, half.y);
    particle.atPx = Turn(glm::dvec2(startX, startY), owner.angleDeg) + owner.atPx;
    particle.bornPx = particle.atPx;
}

void Reset(const System& system, Particle& particle, const Owner& owner, const Random& random) {
    const glm::dvec2 halfDir = system.randomizeDir / 2.0;
    particle.angleDirDeg = system.angleDir + random(-system.randAngle / 2.0, system.randAngle / 2.0);
    particle.elapsedMs = 0.0;
    particle.lifeMs = system.lifeTimeMs + random(-system.randomLifeTimeMs / 2.0, system.randomLifeTimeMs / 2.0);
    particle.size = system.size + random(-system.randomizeSize / 2.0, system.randomizeSize / 2.0);
    // The spread is added BEFORE the turn, and gravity is never turned. x draws
    // before y, in two statements (:487-488), as Release's start point does.
    const double dirX = system.direction.x + random(-halfDir.x, halfDir.x);
    const double dirY = system.direction.y + random(-halfDir.y, halfDir.y);
    particle.velocityPx = Turn(glm::dvec2(dirX, dirY), owner.angleDeg);
    particle.colour = system.colour0;
    Release(system, particle, owner, random);
    if (system.Frames() > 1) {
        if (system.animationMode == 1) {
            particle.frame = 0; // PLAY_ANIMATION
        } else if (system.animationMode == 2) {
            // PICK_RANDOM_FRAME: Randomizer::Int(n - 1), which is 0..n-1 inclusive.
            const int frames = system.Frames();
            particle.frame = std::min(static_cast<int>(random(0.0, static_cast<double>(frames))), frames - 1);
        }
    }
}

std::vector<Particle> MakePool(const System& system, int count, const Owner& owner, const Random& random) {
    std::vector<Particle> pool(static_cast<std::size_t>(std::max(0, count)));
    for (Particle& particle : pool) {
        Reset(system, particle, owner, random);
        particle.released = false;
    }
    return pool;
}

bool Step(const System& system, Particle& particle, int index, int poolSize, const Owner& owner, double elapsedMs,
          bool killed, const Random& random) {
    if (system.repeat > 0 && particle.repeats >= system.repeat) return false;

    // Counted before this frame moves it, where the loop counts it.
    const bool active = particle.size > 0.0 && particle.released && (!killed || particle.elapsedMs < particle.lifeMs);

    const double frameSpeed = std::min(elapsedMs, kFrameCapMs) * kFrameSpeedPerMs;
    // The age takes the whole frame; only the motion is capped.
    particle.elapsedMs += elapsedMs;

    if (!particle.released) {
        // Staggered across one lifetime in pool order, unless the system releases
        // the lot at once. A killed system still releases: :219-231 do not ask.
        const double releaseAt = (system.lifeTimeMs + system.randomLifeTimeMs) *
                                 (static_cast<double>(index) / static_cast<double>(std::max(1, poolSize)));
        if (particle.elapsedMs > releaseAt || system.allAtOnce) {
            particle.elapsedMs = 0.0;
            particle.released = true;
            Release(system, particle, owner, random);
        }
    }

    if (particle.released) {
        particle.velocityPx += system.gravity * frameSpeed;
        particle.atPx += particle.velocityPx * frameSpeed;
        particle.angleDeg += particle.angleDirDeg * frameSpeed;
        particle.size += system.growth * frameSpeed;
        // Divided as the original divides it, a life below 0 included:
        // portal_fail_in_antiportal.ent (350 +/- 402.5 ms) can draw one, whose w is
        // then at most 0 and which is renewed on the next frame. Only a life of
        // exactly 0 is kept from dividing, where the original's float would not be.
        const double w = particle.lifeMs != 0.0 ? particle.elapsedMs / particle.lifeMs : 1.0;
        particle.colour = system.colour0 + (system.colour1 - system.colour0) * w;
        if (system.Frames() > 1 && system.animationMode == 1) {
            particle.frame = std::min(static_cast<int>(static_cast<double>(system.Frames()) * w), system.Frames() - 1);
        }
        particle.size = std::min(particle.size, system.maxSize);
        particle.size = std::max(particle.size, system.minSize);

        if (particle.elapsedMs > particle.lifeMs) {
            ++particle.repeats;
            // Renewed even after its last life; the next frame's first test spends it.
            if (!killed) Reset(system, particle, owner, random);
        }
    }
    return active;
}

bool Drawn(const System& system, const Particle& particle, bool killed) {
    if (system.repeat > 0 && particle.repeats >= system.repeat) return false;
    if (particle.size <= 0.0 || !particle.released || particle.colour.a <= 0.0) return false;
    if (killed && particle.elapsedMs > particle.lifeMs) return false;
    return true;
}

glm::dvec4 DrawColour(const System& system, const Particle& particle, const glm::dvec3& ambient) {
    glm::dvec3 lift(1.0);
    if (system.alphaMode == kAlphaPixel || system.alphaMode == kAlphaTest) {
        lift = glm::min(system.luminance + ambient, glm::dvec3(1.0));
    }
    glm::dvec4 colour = particle.colour * glm::dvec4(lift, 1.0);
    if (system.additive) colour.a = 1.0;
    return colour;
}

glm::dvec2 QuadPx(const Particle& particle) {
    return glm::dvec2(particle.size, particle.size);
}

float WorldRotation(const Particle& particle) {
    return Units::ToWorldRotation(-particle.angleDeg * kPi / 180.0);
}

double SlotFraction(int slot, int systems) {
    const int n = std::max(1, systems);
    const int t = std::clamp(slot, 0, n - 1);
    return 0.25 + 0.25 * static_cast<double>(t + 1) / static_cast<double>(n + 1);
}

} // namespace MagicPortals::Particles
