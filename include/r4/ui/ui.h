// r4/ui/ui.h - a small GUI in the look of the panels of IRIS Performer
// (libpfutil): grey buttons in relief, yellow lights on the switches, blue
// sliders, menu buttons opening their list, messages in yellow on a dark
// sunken box. It is drawn through r4::gpu, so it follows the backends.
//
// It is immediate: each frame, the program calls the widgets with its own
// variables, and they report what the user did.
//
//   ui.beginFrame(width, height, input);
//   ui.beginPanel("settings", {0, 0, 300, height});
//   if (ui.button("Reset")) reset();
//   ui.toggle("Fog", fog);
//   ui.slider("Field of view", fov, 10, 120, "%.0f");
//   ui.endPanel();
//   ui.endFrame();
//   ... draw the scene ...
//   ui.render();
//
// Labels can carry "##" and an identifier after it, not shown: two widgets
// with the same text then stay apart.
#pragma once

#include "r4/gpu/device.h"
#include "r4/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r4::ui {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
    static Color rgb(float r, float g, float b, float a = 1) {
        auto c = [](float v) {
            return (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f);
        };
        return {c(r), c(g), c(b), c(a)};
    }
};

struct Rect {
    float x = 0; // points, from the top left
    float y = 0;
    float w = 0;
    float h = 0;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

enum class Font {
    Regular,
    Bold,
    Small,
    Title
};

// the state of the mouse for a frame
struct Input {
    float mouseX = -1; // points, from the top left
    float mouseY = -1;
    bool left = false;
    bool right = false;
    float wheel = 0; // notches, positive away from the user
    // a button went down, or up, since the last frame (a quick click can do
    // both between two frames)
    bool pressed = false;
    bool released = false;
};

class Context {
public:
    explicit Context(gpu::Device &device);
    ~Context();
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    // --- the frame ------------------------------------------------------------
    // width, height and the mouse in points; pixelScale: the pixels of the
    // framebuffer per point (2 on a Retina screen), for sharp text
    void beginFrame(int width, int height, const Input &input, float pixelScale = 1);
    void endFrame();
    // draws the frame's GUI over what is in the framebuffer
    void render();
    // the mouse is over the GUI (or a widget holds it): not for the scene
    bool wantsMouse() const;

    // --- panels -----------------------------------------------------------------
    // a column of widgets in a rectangle of the window, with a title bar; the
    // wheel scrolls it when its widgets are taller
    void beginPanel(const char *id, Rect r, const char *title = nullptr);
    void endPanel();
    // the next widgets are laid out n per row (1 by default)
    void columns(int n);
    void space(float pixels = 6);
    // the room left below the cursor of the panel
    float remainingHeight() const;

    // --- widgets ---------------------------------------------------------------
    bool button(const char *label, bool enabled = true);
    bool toggle(const char *label, bool &on, bool enabled = true); // a switch with its light
    // a menu button: "label: item", opening the list of the items
    bool menu(const char *label, int &selected, const std::vector<std::string> &items, bool enabled = true);
    // the value in the label, a blue bar under it; log: logarithmic scale
    bool slider(const char *label, float &value, float min, float max, const char *format = "%.2f", bool log = false, bool enabled = true);
    // a row of choices with lights
    bool radio(const char *label, int &selected, const std::vector<std::string> &items, bool enabled = true);
    // the lights of the bits of a mask, in a row
    bool bits(const char *label, uint32_t &mask, int count);
    // a color picker: a button with the swatch, opening a square of
    // saturation and value, a bar of hue, the R G B sliders and the hex code
    bool color(const char *label, Vec3 &color);
    // a header that folds the widgets after it (the caller skips them)
    bool section(const char *label, bool &open);
    void label(const char *text, Font font = Font::Regular);
    // yellow text in a dark sunken box, one line or several ("\n")
    void message(const char *text);
    // name and value lines in a message box
    void table(const std::vector<std::pair<std::string, std::string>> &rows);
    // a curve in a dark box, from 0 to max, with a text over it
    void plot(const float *values, int count, float max, Color curve, const char *overlay, float height = 52);

    // a row of a tree: the arrow opens it, the light shows or hides the
    // node, the label selects it. Returns true when the label is clicked.
    struct TreeRow {
        const void *id = nullptr;
        int depth = 0;
        const char *label = "";
        Color tag; // the color of the type
        const char *tagText = "";
        bool hasChildren = false;
        bool selected = false;
    };
    bool treeRow(const TreeRow &row, bool &open, bool &visible);

    // an image (a texture of true colors, its first row at the top: the
    // texture of an Overlay, a render target) as wide as the panel, at the
    // aspect given, at most maxHeight high
    void image(gpu::TextureHandle texture, float aspect, float maxHeight = 0);
    // an image in a rectangle, anywhere (a canvas, a panel)
    void image(gpu::TextureHandle texture, Rect r);

    // --- drawing, for custom widgets --------------------------------------------
    // a canvas: a rectangle of the window to draw in freely, clipped, without
    // a background; the mouse is the GUI's only over its clickable parts
    void beginCanvas(const char *id, Rect r);
    void endCanvas();
    // true when r is clicked (in a panel or a canvas); id: anything unique
    bool clickable(const void *id, Rect r);
    bool hovered(Rect r) const;
    void line(float x0, float y0, float x1, float y1, Color c, float width = 1);
    Rect next(float height); // the rectangle of the next widget
    void fill(Rect r, Color c);
    void bevel(Rect r, bool sunken);
    float text(float x, float y, const char *s, Color c, Font font = Font::Regular, float maxWidth = 1e9f);
    float textWidth(const char *s, Font font = Font::Regular) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace r4::ui
