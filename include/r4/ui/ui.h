// r4/ui/ui.h - an immediate GUI in the look of the panels of IRIS Performer, drawn through r4::gpu.
// Each frame the program calls the widgets with its own variables, between beginFrame and endFrame, and render() draws them.
// A label can end with "##" and an identifier, not shown, to tell apart two widgets of the same text.
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
    REGULAR,
    BOLD,
    SMALL,
    TITLE
};

// the keys that a text field uses
enum class Key {
    BACKSPACE,
    DELETE_FORWARD,
    LEFT,
    RIGHT,
    HOME,
    END,
    ENTER,
    ESCAPE
};

struct Input {
    float mouseX = -1; // points, from the top left
    float mouseY = -1;
    bool left = false;
    bool right = false;
    float wheel = 0; // notches, positive away from the user
    // since the last frame; a quick click can do both
    bool pressed = false;
    bool released = false;
    // since the last frame, for a text field: the text typed (UTF-8) and the keys pressed
    std::string text;
    std::vector<Key> keys;
};

class Context {
public:
    explicit Context(gpu::Device &device);
    ~Context();
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    // in points; pixelScale: the pixels of the framebuffer per point
    void beginFrame(int width, int height, const Input &input, float pixelScale = 1);
    void endFrame();
    void render();
    // the mouse is over the GUI, or a widget holds it
    bool wantsMouse() const;
    // a text field has the focus: the keys are for it
    bool wantsKeyboard() const;

    // a column of widgets with a title bar, scrolled by the wheel
    // open: with a title, a cross at its right sets it to false
    void beginPanel(const char *id, Rect r, const char *title = nullptr, bool *open = nullptr);
    void endPanel();
    // n widgets per row
    void columns(int n);
    void space(float pixels = 6);
    float remainingHeight() const;

    bool button(const char *label, bool enabled = true);
    bool toggle(const char *label, bool &on, bool enabled = true);
    // "label: item", opening the list of the items
    bool menu(const char *label, int &selected, const std::vector<std::string> &items, bool enabled = true);
    // the label on the left, the item on the right, opening the list of the items
    bool select(const char *label, int &selected, const std::vector<std::string> &items, bool enabled = true);
    // the label on the left, the text on the right: a click edits it, Enter or a click elsewhere keeps it (true when it
    // changed), Escape gives it back
    bool inputText(const char *label, std::string &text, bool enabled = true);
    // a row of tabs over the width of the panel; true when the selected one changed
    bool tabs(const char *id, int &selected, const std::vector<std::string> &names);
    // a framed area of the panel with its own scroll and scroll bar, the rest of the panel when height is 0; the widgets
    // until endBox go in it. Just under tabs, it opens on the selected tab.
    void beginBox(const char *id, float height = 0);
    void endBox();
    // log: logarithmic scale
    bool slider(const char *label, float &value, float min, float max, const char *format = "%.2f", bool log = false, bool enabled = true);
    bool radio(const char *label, int &selected, const std::vector<std::string> &items, bool enabled = true);
    bool bits(const char *label, uint32_t &mask, int count);
    bool color(const char *label, Vec3 &color);
    // folds the widgets after it: the caller skips them
    bool section(const char *label, bool &open);
    void label(const char *text, Font font = Font::REGULAR);
    // one line or several ("\n")
    void message(const char *text);
    void table(const std::vector<std::pair<std::string, std::string>> &rows);
    // from 0 to max, with a text over it
    void plot(const float *values, int count, float max, Color curve, const char *overlay, float height = 52);

    // true when the label is clicked; the arrow opens it, the light shows or hides the node
    struct TreeRow {
        const void *id = nullptr;
        int depth = 0;
        const char *label = "";
        Color tag;
        const char *tagText = "";
        bool hasChildren = false;
        bool selected = false;
    };
    bool treeRow(const TreeRow &row, bool &open, bool &visible);

    // a texture of true colors, its first row at the top, as wide as the panel, at most maxHeight high
    void image(gpu::TextureHandle texture, float aspect, float maxHeight = 0);
    void image(gpu::TextureHandle texture, Rect r);

    // a rectangle of the window to draw in freely, clipped; the mouse is the GUI's only over its clickable parts
    void beginCanvas(const char *id, Rect r);
    void endCanvas();
    // id: anything unique
    bool clickable(const void *id, Rect r);
    bool hovered(Rect r) const;
    void line(float x0, float y0, float x1, float y1, Color c, float width = 1);
    Rect next(float height);
    void fill(Rect r, Color c);
    void bevel(Rect r, bool sunken);
    float text(float x, float y, const char *s, Color c, Font font = Font::REGULAR, float maxWidth = 1e9f);
    float textWidth(const char *s, Font font = Font::REGULAR) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace r4::ui
