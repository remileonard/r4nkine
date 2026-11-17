// r4/ui/controls.h - ready-made groups of widgets for the parts of R4nkine:
// put them in a panel of a program (r4fly and the examples use them).
#pragma once

#include "r4/raster/raster.h"
#include "r4/render/postprocess.h"
#include "r4/ui/ui.h"

#include <string>
#include <vector>

namespace r4::ui {

// the palettes the controls offer: VGA 256, EGA 16, CGA 4, handheld 4
// greens, amber 8, grey 16
struct NamedPalette {
    std::string name;
    raster::Palette palette;
    int size;
};
const std::vector<NamedPalette> &palettePresets();

// the settings of a post process: antialiasing, colors, palette, CRT;
// true when one changed
bool postProcessControls(Context &gui, render::PostSettings &settings);

} // namespace r4::ui
