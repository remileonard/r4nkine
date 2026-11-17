// controls.cpp - the widgets of the settings of R4nkine's parts.
#include "r4/ui/controls.h"

namespace r4::ui {

const std::vector<NamedPalette> &palettePresets() {
    static const std::vector<NamedPalette> presets = [] {
        using raster::rgba;
        std::vector<NamedPalette> p;
        raster::Palette vga = raster::Palette::vga();
        p.push_back({"VGA 256", vga, 256});
        p.push_back({"EGA 16", vga, 16});
        raster::Palette cga;
        cga.colors[0] = rgba(0, 0, 0);
        cga.colors[1] = rgba(85, 255, 255);
        cga.colors[2] = rgba(255, 85, 255);
        cga.colors[3] = rgba(255, 255, 255);
        p.push_back({"CGA 4", cga, 4});
        p.push_back({"Handheld 4", raster::Palette::ramp(rgba(15, 56, 15), rgba(155, 188, 15), 4), 4});
        p.push_back({"Amber 8", raster::Palette::ramp(rgba(20, 8, 0), rgba(255, 176, 0), 8), 8});
        p.push_back({"Grey 16", raster::Palette::ramp(rgba(0, 0, 0), rgba(255, 255, 255), 16), 16});
        return p;
    }();
    return presets;
}

bool postProcessControls(Context &g, render::PostSettings &s) {
    bool changed = false;
    int msaa = s.msaa >= 8 ? 3 : s.msaa >= 4 ? 2 : s.msaa >= 2 ? 1 : 0;
    if (g.menu("MSAA", msaa, {"Off", "2x", "4x", "8x"})) {
        s.msaa = 1 << msaa;
        changed = true;
    }
    changed |= g.toggle("FXAA", s.fxaa);
    float pixelate = (float)s.pixelate;
    if (g.slider("Big Pixels", pixelate, 1, 16, "%.0f")) {
        s.pixelate = (int)pixelate;
        changed = true;
    }
    g.label("Colors", Font::Bold);
    changed |= g.slider("Brightness", s.brightness, -0.5f, 0.5f, "%.2f");
    changed |= g.slider("Contrast", s.contrast, 0.2f, 3, "%.2f", true);
    changed |= g.slider("Saturation", s.saturation, 0, 2, "%.2f");
    changed |= g.slider("Gamma", s.gamma, 0.3f, 3, "%.2f", true);
    changed |= g.color("Tint", s.tint);
    changed |= g.slider("Fade", s.fade, 0, 1, "%.2f");
    if (s.fade > 0)
        changed |= g.color("Fade Color", s.fadeColor);
    g.label("Palette", Font::Bold);
    changed |= g.toggle("Reduce to a Palette", s.quantize);
    if (s.quantize) {
        const auto &presets = palettePresets();
        std::vector<std::string> names;
        int current = 0;
        for (size_t i = 0; i < presets.size(); i++) {
            names.push_back(presets[i].name);
            if (presets[i].size == s.paletteSize && presets[i].palette.colors == s.palette.colors)
                current = (int)i;
        }
        if (g.menu("Palette", current, names)) {
            s.palette = presets[current].palette;
            s.paletteSize = presets[current].size;
            changed = true;
        }
        changed |= g.toggle("Dither", s.dither);
    }
    g.label("Screen", Font::Bold);
    changed |= g.toggle("Scanlines", s.scanlines);
    if (s.scanlines) {
        changed |= g.slider("Line Darkness", s.scanlineStrength, 0, 1, "%.2f");
        changed |= g.slider("Line Size", s.scanlineSize, 1, 8, "%.1f px");
    }
    changed |= g.slider("Curvature", s.curvature, 0, 0.3f, "%.2f");
    changed |= g.slider("Aberration", s.aberration, 0, 4, "%.1f px");
    changed |= g.slider("Vignette", s.vignette, 0, 1, "%.2f");
    if (g.button("No Effect")) {
        s = render::PostSettings{};
        changed = true;
    }
    return changed;
}

} // namespace r4::ui
