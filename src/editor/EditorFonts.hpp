#pragma once

struct ImFont;

namespace Supersonic {

// The editor's typefaces.
//
// Before this the whole editor rendered in ImGui's built-in ProggyClean - a
// 13-pixel bitmap face designed for debug overlays, which is exactly what it
// made a finished tool look like. Inter is a UI face drawn for small sizes on
// screen, and Font Awesome is merged into the same atlas so an icon and its
// label can live in one string.
//
// Both are EMBEDDED rather than loaded from assets/. Everything else in the
// engine resolves asset paths relative to the working directory, and
// GamePackager builds standalone folders - a font read from disk is one more
// thing to ship and one more way a packaged build starts with no text at all.
class EditorFonts {
public:
    // Builds the atlas. Must be called after ImGui::CreateContext() and BEFORE
    // the Vulkan backend is initialised: building it afterwards means tearing
    // down and re-uploading a font texture the backend may already have
    // recorded into a command buffer, which is a class of bug this renderer has
    // been bitten by twice already.
    //
    // dpiScale comes from the monitor; 1.0 is a 96-DPI display.
    static void Load(float dpiScale);

    // Body text, with the icons merged in. Also the default font.
    static ImFont* Body();

    // Semibold, for panel section headings. Slightly larger than the body.
    static ImFont* Heading();

    // Semibold at body size, for emphasis inside a row.
    static ImFont* Strong();

    // The size Load() was called with, so callers can scale padding to match.
    static float BodySize();
    static float Scale();
};

} // namespace Supersonic
