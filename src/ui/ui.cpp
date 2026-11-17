// ui.cpp - the GUI of r4::ui, in the look of the panels of IRIS Performer
// (pfuGUI): the colors and the reliefs are those of the port of perfly in
// legacy_opengl_demo.
#include "r4/ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>

namespace r4::ui {

namespace {
#include "font_data.inc"

// --- the colors of pfuGUI -----------------------------------------------------
const Color kPanel = Color::rgb(0.55f, 0.55f, 0.6f);
const Color kFace = Color::rgb(0.78f, 0.78f, 0.82f);
const Color kFaceHot = Color::rgb(0.83f, 0.83f, 0.87f);
const Color kFacePressed = Color::rgb(0.62f, 0.62f, 0.68f);
const Color kSliderFace = Color::rgb(0.72f, 0.72f, 0.76f);
const Color kTrack = Color::rgb(0.45f, 0.45f, 0.5f);
const Color kBlue = Color::rgb(0.35f, 0.45f, 0.85f);
const Color kKnob = Color::rgb(0.85f, 0.85f, 0.88f);
const Color kHigh = Color::rgb(0.95f, 0.95f, 0.95f);
const Color kLow = Color::rgb(0.35f, 0.35f, 0.35f);
const Color kLightOn = Color::rgb(1.0f, 0.9f, 0.1f);
const Color kLightOff = Color::rgb(0.35f, 0.35f, 0.38f);
const Color kDark = Color::rgb(0.12f, 0.12f, 0.16f);
const Color kYellow = Color::rgb(1, 1, 0.5f);
const Color kText = Color::rgb(0, 0, 0.1f);
const Color kTextOff = Color::rgb(0.45f, 0.45f, 0.45f);
const Color kWhite = Color::rgb(1, 1, 1);
const Color kHeader = Color::rgb(0.42f, 0.42f, 0.5f);
const Color kMenuFace = Color::rgb(0.85f, 0.85f, 0.88f);

constexpr float kRowH = 21, kGap = 3, kMargin = 4, kTitleH = 20;

struct Vertex {
    float x, y, u, v;
    Color c;
};

struct Command {
    bool clip = false;
    Rect rect;
    uint32_t first = 0, count = 0;
};

uint32_t hashOf(const char *s, uint32_t h) {
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

// the text shown: the label before "##"
std::string shown(const char *label) {
    const char *p = std::strstr(label, "##");
    return p ? std::string(label, p) : std::string(label);
}

// the next Latin-1 code of an UTF-8 string, '?' outside
int nextCode(const char *&s) {
    unsigned char c = (unsigned char)*s++;
    if (c < 0x80)
        return c;
    if ((c & 0xe0) == 0xc0 && (*s & 0xc0) == 0x80) {
        int code = (c & 0x1f) << 6 | (*s++ & 0x3f);
        return code <= 255 ? code : '?';
    }
    while ((*s & 0xc0) == 0x80)
        s++;
    return '?';
}

int glyphIndex(int code) {
    if (code >= 32 && code <= 126)
        return code - 32;
    if (code >= 160 && code <= 255)
        return 95 + code - 160;
    return '?' - 32;
}

void formatValue(char *buf, size_t n, const char *fmt, float v) { std::snprintf(buf, n, fmt, v); }

} // namespace

struct Context::Impl {
    gpu::Device &device;
    gpu::ShaderHandle shader;
    gpu::PipelineHandle pipeline;
    gpu::TextureHandle atlas;
    gpu::BufferHandle vbo, ubo;
    size_t vboSize = 0;
    float whiteU = 0, whiteV = 0;

    int width = 0, height = 0; // in points
    float pixelScale = 1;      // pixels per point
    int fontBase = 0;          // the fonts at 1x (0) or 2x (4)
    float fontDiv = 1;         // their pixels per point
    Input in, prev;
    bool pressed = false, released = false;
    std::vector<Vertex> verts;
    std::vector<Command> cmds;

    // the widget held by the mouse, the one under it
    uint32_t active = 0, hot = 0;
    bool activePressedInside = false;

    // the panels: their rectangles (for wantsMouse), their scrolls
    struct PanelState {
        float scroll = 0, contentH = 0;
    };
    std::map<uint32_t, PanelState> panels;
    std::vector<Rect> panelRects, lastPanelRects;
    bool inPanel = false;
    uint32_t panelId = 0;
    Rect panel, clip;
    float cursorY = 0;
    int cols = 1, col = 0;
    float rowY = 0, rowH = 0;
    PanelState *ps = nullptr;

    // the open menu: its widget, the place of the list, its items
    uint32_t menuId = 0;
    Rect menuRect;
    std::vector<std::string> menuItems;
    int menuSelected = -1;
    int menuChoice = -1; // chosen this frame, given at the next call
    uint32_t menuChoiceId = 0;

    explicit Impl(gpu::Device &dev) : device(dev) {}

    // --- drawing ---------------------------------------------------------------
    void setClip(bool on, Rect r) {
        if (!cmds.empty() && cmds.back().count == 0) {
            cmds.back().clip = on;
            cmds.back().rect = r;
            return;
        }
        Command c;
        c.clip = on;
        c.rect = r;
        c.first = (uint32_t)verts.size();
        cmds.push_back(c);
    }
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c) {
        Vertex a{x0, y0, u0, v0, c}, b{x1, y0, u1, v0, c}, cc{x1, y1, u1, v1, c}, dd{x0, y1, u0, v1, c};
        verts.insert(verts.end(), {a, b, cc, a, cc, dd});
        cmds.back().count += 6;
    }
    void fill(Rect r, Color c) { quad(r.x, r.y, r.x + r.w, r.y + r.h, whiteU, whiteV, whiteU, whiteV, c); }
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
        verts.push_back({x0, y0, whiteU, whiteV, c});
        verts.push_back({x1, y1, whiteU, whiteV, c});
        verts.push_back({x2, y2, whiteU, whiteV, c});
        cmds.back().count += 3;
    }
    // the relief of pfuGUI: light top and left, dark bottom and right
    void bevel(Rect r, bool sunken) {
        Color hi = sunken ? kLow : kHigh, lo = sunken ? kHigh : kLow;
        fill({r.x, r.y, r.w, 1}, hi);
        fill({r.x, r.y, 1, r.h}, hi);
        fill({r.x, r.y + r.h - 1, r.w, 1}, lo);
        fill({r.x + r.w - 1, r.y, 1, r.h}, lo);
    }
    void box(Rect r, Color c, bool sunken) {
        fill(r, c);
        bevel(r, sunken);
    }
    void light(float cx, float cy, float rad, bool on, bool enabled) {
        Color c = on ? (enabled ? kLightOn : Color::rgb(0.6f, 0.6f, 0.3f)) : kLightOff;
        Rect r{std::floor(cx - rad), std::floor(cy - rad), std::floor(2 * rad), std::floor(2 * rad)};
        box(r, c, true);
    }

    float glyphAdvance(int font, int code) const {
        return kGlyphMetrics[font + fontBase][glyphIndex(code)][2] / fontDiv;
    }
    float textWidth(const char *s, Font font) const {
        float w = 0;
        while (*s) {
            int code = nextCode(s);
            if (code == '\n')
                break;
            w += glyphAdvance((int)font, code);
        }
        return w;
    }
    // y: the top of the line; returns the width drawn
    float text(float x, float y, const char *s, Color c, Font font, float maxWidth) {
        int f = (int)font + fontBase;
        float k = 1 / fontDiv;
        float pen = std::floor(x), base = std::floor(y + kFontMetrics[f][1] * k);
        float start = pen;
        while (*s) {
            int code = nextCode(s);
            if (code == '\n')
                break;
            int g = glyphIndex(code);
            float adv = kGlyphMetrics[f][g][2] * k;
            if (pen + adv - start > maxWidth)
                break;
            const short *b = kGlyphBox[f][g];
            if (b[2] > b[0]) {
                // on the pixels of the screen
                float gx = std::floor((pen + kGlyphMetrics[f][g][0] * k) * pixelScale + 0.5f) / pixelScale;
                float gy = std::floor((base + kGlyphMetrics[f][g][1] * k) * pixelScale + 0.5f) / pixelScale;
                float w = (b[2] - b[0]) * k, h = (b[3] - b[1]) * k;
                quad(gx, gy, gx + w, gy + h, b[0] / (float)kAtlasWidth, 1 - b[1] / (float)kAtlasHeight,
                     b[2] / (float)kAtlasWidth, 1 - b[3] / (float)kAtlasHeight, c);
            }
            pen += adv;
        }
        return pen - start;
    }
    float lineHeight(Font f) const {
        int i = (int)f;
        return std::ceil(kFontMetrics[i][1] - kFontMetrics[i][2]); // the 1x metrics: the layout in points
    }
    // the text centered vertically in a rectangle
    void textIn(Rect r, float dx, const char *s, Color c, Font f = Font::Regular, bool center = false) {
        float th = lineHeight(f);
        float y = r.y + (r.h - th) * 0.5f;
        float maxw = r.w - dx - 3;
        if (center) {
            float w = std::min(textWidth(s, f), maxw);
            text(r.x + (r.w - w) * 0.5f, y, s, c, f, maxw);
        } else {
            text(r.x + dx, y, s, c, f, maxw);
        }
    }

    // --- layout ---------------------------------------------------------------
    Rect next(float h) {
        if (!inPanel)
            return {};
        float inner = panel.w - 2 * kMargin;
        float w = (inner - (cols - 1) * kGap) / cols;
        if (col == 0) {
            rowY = cursorY;
            rowH = 0;
        }
        Rect r{panel.x + kMargin + col * (w + kGap), rowY, w, h};
        rowH = std::max(rowH, h);
        col++;
        if (col >= cols) {
            col = 0;
            cursorY = rowY + rowH + kGap;
        } else {
            cursorY = rowY + rowH + kGap;
        }
        r.x = std::floor(r.x);
        r.w = std::floor(r.w);
        return r;
    }

    // --- interaction ------------------------------------------------------------
    bool visible(Rect r) const { return r.y + r.h > clip.y && r.y < clip.y + clip.h; }
    bool hover(Rect r) const {
        return menuId == 0 && inPanel && clip.contains(in.mouseX, in.mouseY) && r.contains(in.mouseX, in.mouseY);
    }
    // the behavior of a button: true when released over it
    bool behave(uint32_t id, Rect r, bool enabled, bool *held = nullptr) {
        bool over = hover(r);
        if (over)
            hot = id;
        bool click = false;
        if (enabled && over && pressed && active == 0)
            active = id;
        if (active == id) {
            if (released) {
                click = over;
                active = 0;
            }
        }
        if (held)
            *held = active == id && over;
        return click;
    }
    uint32_t idOf(const char *label) const { return hashOf(label, panelId * 31u + 2166136261u); }
};

Context::Context(gpu::Device &device) : d(std::make_unique<Impl>(device)) {
    gpu::Device &dev = device;
    // the atlas, rows from the bottom for the device
    std::vector<uint8_t> top((size_t)kAtlasWidth * kAtlasHeight), bottom(top.size());
    size_t o = 0;
    for (size_t i = 0; i + 1 < sizeof kAtlasRuns; i += 2)
        for (int k = 0; k < kAtlasRuns[i] && o < top.size(); k++)
            top[o++] = kAtlasRuns[i + 1];
    for (int y = 0; y < kAtlasHeight; y++)
        std::memcpy(&bottom[(size_t)y * kAtlasWidth], &top[(size_t)(kAtlasHeight - 1 - y) * kAtlasWidth], kAtlasWidth);
    gpu::TextureDesc td;
    td.width = kAtlasWidth;
    td.height = kAtlasHeight;
    td.format = gpu::TextureFormat::R8;
    td.minFilter = td.magFilter = gpu::Filter::Nearest;
    td.mipmaps = false;
    td.wrapS = td.wrapT = gpu::Wrap::Clamp;
    d->atlas = dev.createTexture(td, bottom.data());
    // the white texels: the bottom right corner of the atlas (rows from the top)
    d->whiteU = (kAtlasWidth - 1.0f) / kAtlasWidth;
    d->whiteV = 1 - (kAtlasHeight - 1.0f) / kAtlasHeight;

    gpu::ShaderDesc sd;
    sd.debugName = "r4::ui";
    sd.vertex = R"(#version 330
layout(std140) uniform Screen { vec4 size; };
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec4 color;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = uv;
    vColor = color;
    gl_Position = vec4(position.x / size.x * 2.0 - 1.0, 1.0 - position.y / size.y * 2.0, 0.0, 1.0);
}
)";
    sd.fragment = R"(#version 330
uniform sampler2D atlas;
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vec4(vColor.rgb, vColor.a * texture(atlas, vUV).r); }
)";
    sd.uniformBlocks = {{"Screen", 0}};
    sd.samplers = {{"atlas", 0}};
    d->shader = dev.createShader(sd);
    gpu::PipelineDesc pd;
    pd.shader = d->shader;
    gpu::VertexStream vs;
    vs.stride = sizeof(Vertex);
    vs.attribs = {{0, 2, gpu::AttribType::Float, false, 0},
                  {1, 2, gpu::AttribType::Float, false, 8},
                  {2, 4, gpu::AttribType::UByte, true, 16}};
    pd.layout.streams = {vs};
    pd.cull = gpu::CullMode::None;
    pd.blend = gpu::BlendMode::Alpha;
    pd.depthTest = false;
    pd.depthWrite = false;
    d->pipeline = dev.createPipeline(pd);
    float screen[4] = {1, 1, 0, 0};
    d->ubo = dev.createBuffer(gpu::BufferUsage::Uniform, screen, sizeof screen, true);
}

Context::~Context() {
    gpu::Device &dev = d->device;
    if (d->vbo)
        dev.destroyBuffer(d->vbo);
    dev.destroyBuffer(d->ubo);
    dev.destroyPipeline(d->pipeline);
    dev.destroyShader(d->shader);
    dev.destroyTexture(d->atlas);
}

void Context::beginFrame(int width, int height, const Input &input, float pixelScale) {
    Impl &s = *d;
    s.width = width;
    s.height = height;
    s.pixelScale = pixelScale > 0 ? pixelScale : 1;
    s.fontBase = s.pixelScale >= 1.5f ? 4 : 0;
    s.fontDiv = s.fontBase ? 2.0f : 1.0f;
    s.prev = s.in;
    s.in = input;
    s.pressed = input.pressed || (input.left && !s.prev.left) || (input.right && !s.prev.right);
    s.released = input.released || (!input.left && s.prev.left) || (!input.right && s.prev.right);
    s.verts.clear();
    s.cmds.clear();
    s.setClip(false, {});
    s.hot = 0;
    s.lastPanelRects = s.panelRects;
    s.panelRects.clear();

    // the open menu takes the clicks: an item, or elsewhere to close it
    if (s.menuId) {
        bool inList = s.menuRect.contains(input.mouseX, input.mouseY);
        if (s.released && inList) {
            int item = (int)((input.mouseY - s.menuRect.y) / (s.menuRect.h / std::max<size_t>(1, s.menuItems.size())));
            s.menuChoice = std::max(0, std::min(item, (int)s.menuItems.size() - 1));
            s.menuChoiceId = s.menuId;
            s.menuId = 0;
            s.pressed = s.released = false;
        } else if (s.pressed && !inList) {
            s.menuId = 0;
            s.active = 0;
            s.pressed = false; // the click only closes the menu
        }
    }
    if (!input.left && !input.right && !s.released)
        s.active = 0;
}

void Context::endFrame() {
    Impl &s = *d;
    if (!s.menuId)
        return;
    // the list of the open menu, over everything
    s.setClip(false, {});
    float ih = s.menuRect.h / std::max<size_t>(1, s.menuItems.size());
    for (size_t i = 0; i < s.menuItems.size(); i++) {
        Rect r{s.menuRect.x, s.menuRect.y + i * ih, s.menuRect.w, ih};
        bool over = r.contains(s.in.mouseX, s.in.mouseY);
        s.box(r, over ? kBlue : kMenuFace, false);
        if ((int)i == s.menuSelected)
            s.light(r.x + 8, r.y + ih * 0.5f, 3, true, true);
        s.textIn(r, 16, s.menuItems[i].c_str(), over ? kWhite : kText);
    }
}

void Context::render() {
    Impl &s = *d;
    gpu::Device &dev = s.device;
    if (s.verts.empty() || !s.pipeline)
        return;
    size_t bytes = s.verts.size() * sizeof(Vertex);
    if (bytes > s.vboSize) {
        if (s.vbo)
            dev.destroyBuffer(s.vbo);
        s.vboSize = std::max(bytes * 2, (size_t)65536);
        s.vbo = dev.createBuffer(gpu::BufferUsage::Vertex, nullptr, s.vboSize, true);
    }
    dev.updateBuffer(s.vbo, 0, s.verts.data(), bytes);
    float screen[4] = {(float)s.width, (float)s.height, 0, 0};
    dev.updateBuffer(s.ubo, 0, screen, sizeof screen);
    gpu::ClearValues none;
    none.color = none.depth = false;
    // the points of the layout on the pixels of the framebuffer
    float k = s.pixelScale;
    int pw = (int)std::lround(s.width * k), ph = (int)std::lround(s.height * k);
    dev.beginPass(0, 0, pw, ph, none);
    dev.bindPipeline(s.pipeline);
    dev.bindVertexBuffer(0, s.vbo);
    dev.bindUniformBuffer(0, s.ubo, 0, sizeof screen);
    dev.bindTexture(0, s.atlas);
    for (const Command &c : s.cmds) {
        if (!c.count)
            continue;
        if (c.clip) {
            int x0 = (int)std::floor(c.rect.x * k), y0 = (int)std::floor(c.rect.y * k);
            int x1 = (int)std::ceil((c.rect.x + c.rect.w) * k), y1 = (int)std::ceil((c.rect.y + c.rect.h) * k);
            dev.setScissor(x0, ph - y1, x1 - x0, y1 - y0);
        } else {
            dev.clearScissor();
        }
        dev.draw(c.count, c.first);
    }
    dev.clearScissor();
    dev.endPass();
}

bool Context::wantsMouse() const {
    const Impl &s = *d;
    if (s.menuId || s.active)
        return true;
    for (const Rect &r : s.panelRects.empty() ? s.lastPanelRects : s.panelRects)
        if (r.contains(s.in.mouseX, s.in.mouseY))
            return true;
    return false;
}

// --- panels ---------------------------------------------------------------------

void Context::beginPanel(const char *id, Rect r, const char *title) {
    Impl &s = *d;
    s.panelId = hashOf(id, 2166136261u);
    s.ps = &s.panels[s.panelId];
    s.panel = r;
    s.panelRects.push_back(r);
    s.setClip(true, r);
    s.fill(r, kPanel);
    s.bevel(r, false);
    float top = r.y + 1;
    if (title) {
        Rect t{r.x + 2, r.y + 2, r.w - 4, kTitleH};
        s.box(t, kDark, true);
        s.textIn(t, 6, title, kYellow, Font::Bold);
        top = t.y + t.h + 1;
    }
    s.clip = {r.x + 1, top, r.w - 2, r.y + r.h - 1 - top};
    // the wheel scrolls the panel under the mouse
    if (s.menuId == 0 && s.in.wheel != 0 && s.clip.contains(s.in.mouseX, s.in.mouseY))
        s.ps->scroll -= s.in.wheel * 40;
    float maxScroll = std::max(0.0f, s.ps->contentH - s.clip.h);
    s.ps->scroll = std::max(0.0f, std::min(s.ps->scroll, maxScroll));
    s.setClip(true, s.clip);
    s.inPanel = true;
    s.cursorY = s.clip.y + kMargin - s.ps->scroll;
    s.cols = 1;
    s.col = 0;
}

void Context::endPanel() {
    Impl &s = *d;
    if (s.col)
        s.col = 0;
    float contentTop = s.clip.y - s.ps->scroll;
    s.ps->contentH = s.cursorY - contentTop + kMargin;
    // the scroll bar, when the widgets are taller than the panel
    if (s.ps->contentH > s.clip.h + 1) {
        float frac = s.clip.h / s.ps->contentH;
        float pos = s.ps->scroll / s.ps->contentH;
        Rect bar{s.clip.x + s.clip.w - 4, s.clip.y + pos * s.clip.h, 3, std::max(12.0f, frac * s.clip.h)};
        s.fill(bar, kTrack);
    }
    s.inPanel = false;
    s.setClip(false, {});
}

void Context::columns(int n) {
    d->cols = std::max(1, n);
    d->col = 0;
}

void Context::space(float pixels) {
    if (d->col) {
        d->col = 0;
    }
    d->cursorY += pixels;
}

float Context::remainingHeight() const { return d->clip.y + d->clip.h - d->cursorY - kMargin; }

Rect Context::next(float height) { return d->next(height); }
void Context::fill(Rect r, Color c) { d->fill(r, c); }
void Context::bevel(Rect r, bool sunken) { d->bevel(r, sunken); }
float Context::text(float x, float y, const char *s, Color c, Font font, float maxWidth) {
    return d->text(x, y, s, c, font, maxWidth);
}
float Context::textWidth(const char *s, Font font) const { return d->textWidth(s, font); }

// --- widgets ----------------------------------------------------------------------

bool Context::button(const char *label, bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH);
    uint32_t id = s.idOf(label);
    bool held = false;
    bool click = s.behave(id, r, enabled, &held);
    if (s.visible(r)) {
        s.box(r, held ? kFacePressed : s.hot == id && enabled ? kFaceHot : kFace, held);
        s.textIn(r, 0, shown(label).c_str(), enabled ? kText : kTextOff, Font::Regular, true);
    }
    return click;
}

bool Context::toggle(const char *label, bool &on, bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH);
    uint32_t id = s.idOf(label);
    bool held = false;
    bool click = s.behave(id, r, enabled, &held);
    if (click)
        on = !on;
    if (s.visible(r)) {
        s.box(r, held ? kFacePressed : s.hot == id && enabled ? kFaceHot : kFace, held);
        float rad = std::floor(r.h * 0.22f);
        s.textIn({r.x, r.y, r.w - 3 * rad - 4, r.h}, 5, shown(label).c_str(), enabled ? kText : kTextOff);
        s.light(r.x + r.w - 2 * rad - 4, r.y + r.h * 0.5f, rad, on, enabled);
    }
    return click;
}

bool Context::menu(const char *label, int &selected, const std::vector<std::string> &items, bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH);
    uint32_t id = s.idOf(label);
    bool changed = false;
    if (s.menuChoiceId == id && s.menuChoice >= 0) {
        changed = selected != s.menuChoice;
        selected = s.menuChoice;
        s.menuChoiceId = 0;
        s.menuChoice = -1;
    }
    bool over = s.hover(r);
    if (over)
        s.hot = id;
    if (enabled && over && s.pressed && s.active == 0 && !items.empty()) {
        // the list under the button, or above it when there is no room
        float ih = std::max(18.0f, r.h * 0.9f);
        float h = ih * items.size();
        float y = r.y + r.h;
        if (y + h > s.height)
            y = std::max(0.0f, r.y - h);
        s.menuId = id;
        s.menuRect = {r.x, y, std::max(r.w, 120.0f), h};
        s.menuItems = items;
        s.menuSelected = selected;
        s.pressed = false;
    }
    if (s.visible(r)) {
        bool open = s.menuId == id;
        s.box(r, open ? kFacePressed : s.hot == id && enabled ? kFaceHot : kFace, open);
        std::string t = shown(label);
        if (selected >= 0 && selected < (int)items.size())
            t += (t.empty() ? "" : ": ") + items[selected];
        float a = std::floor(r.h * 0.2f);
        s.textIn({r.x, r.y, r.w - 2 * a - 6, r.h}, 5, t.c_str(), enabled ? kText : kTextOff);
        float cx = r.x + r.w - a - 6, cy = r.y + r.h * 0.5f;
        s.triangle(cx - a, cy - a * 0.5f, cx + a, cy - a * 0.5f, cx, cy + a * 0.6f, enabled ? kText : kTextOff);
    }
    return changed;
}

bool Context::slider(const char *label, float &value, float min, float max, const char *format, bool log,
                     bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH + 13);
    uint32_t id = s.idOf(label);
    Rect track{r.x + 5, r.y + r.h - 12, r.w - 10, 7};
    float lo = min, hi = max;
    if (log && lo <= 0)
        lo = hi > 0 ? hi * 1e-4f : 1e-4f;
    auto toT = [&](float v) {
        float t;
        if (log)
            t = hi > lo && v > 0 ? std::log(std::max(v, lo) / lo) / std::log(hi / lo) : 0;
        else
            t = hi > lo ? (v - lo) / (hi - lo) : 0;
        return std::max(0.0f, std::min(1.0f, t));
    };
    auto fromT = [&](float t) {
        t = std::max(0.0f, std::min(1.0f, t));
        return log ? lo * std::pow(hi / lo, t) : lo + t * (hi - lo);
    };
    bool over = s.hover(r);
    if (over)
        s.hot = id;
    bool changed = false;
    if (enabled && over && s.pressed && s.active == 0)
        s.active = id;
    if (s.active == id && (s.in.left || s.in.right || s.pressed)) {
        float v = fromT((s.in.mouseX - track.x) / track.w);
        if (v != value) {
            value = v;
            changed = true;
        }
    }
    if (s.visible(r)) {
        s.box(r, kSliderFace, false);
        char val[64], buf[256];
        formatValue(val, sizeof val, format, value);
        std::snprintf(buf, sizeof buf, "%s: %s", shown(label).c_str(), val);
        s.textIn({r.x, r.y + 1, r.w, r.h - 12}, 5, buf, enabled ? kText : kTextOff);
        float t = toT(value);
        s.box(track, kTrack, true);
        if (enabled)
            s.fill({track.x + 1, track.y + 1, std::floor(t * (track.w - 2)), track.h - 2}, kBlue);
        float kx = std::floor(track.x + t * track.w);
        s.box({kx - 3, track.y - 2, 7, track.h + 4}, s.active == id ? kHigh : kKnob, false);
    }
    return changed;
}

bool Context::radio(const char *label, int &selected, const std::vector<std::string> &items, bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH);
    uint32_t id = s.idOf(label);
    std::string name = shown(label);
    float lw = name.empty() ? 0 : std::min(r.w * 0.3f, s.textWidth(name.c_str(), Font::Regular) + 10);
    int n = (int)items.size();
    float cw = n ? (r.w - lw) / n : 0;
    bool changed = false;
    bool over = s.hover(r);
    if (over)
        s.hot = id;
    if (enabled && over && s.pressed && s.active == 0 && n && s.in.mouseX >= r.x + lw) {
        int i = std::min(n - 1, (int)((s.in.mouseX - r.x - lw) / cw));
        changed = i != selected;
        selected = i;
    }
    if (s.visible(r)) {
        s.box(r, kFace, false);
        if (lw > 0)
            s.textIn({r.x, r.y, lw, r.h}, 5, name.c_str(), enabled ? kText : kTextOff);
        float rad = std::floor(r.h * 0.16f);
        for (int i = 0; i < n; i++) {
            Rect c{r.x + lw + i * cw, r.y, cw, r.h};
            if (i == selected)
                s.fill({c.x + 2, c.y + 2, c.w - 4, c.h - 4}, kFacePressed);
            s.light(c.x + rad + 5, c.y + c.h * 0.5f, rad, i == selected, enabled);
            s.textIn({c.x + 2 * rad + 8, c.y, c.w - 2 * rad - 8, c.h}, 0, items[i].c_str(),
                     enabled ? kText : kTextOff);
        }
    }
    return changed;
}

bool Context::bits(const char *label, uint32_t &mask, int count) {
    Impl &s = *d;
    Rect r = s.next(kRowH + 13);
    uint32_t id = s.idOf(label);
    bool changed = false;
    float cw = (r.w - 10) / std::max(1, count);
    Rect cells{r.x + 5, r.y + r.h - 13, cw * count, 10};
    bool over = s.hover(cells);
    if (over)
        s.hot = id;
    if (over && s.pressed && s.active == 0) {
        int i = std::min(count - 1, (int)((s.in.mouseX - cells.x) / cw));
        mask ^= 1u << i;
        changed = true;
    }
    if (s.visible(r)) {
        s.box(r, kSliderFace, false);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s: %08X", shown(label).c_str(), mask);
        s.textIn({r.x, r.y + 1, r.w, r.h - 13}, 5, buf, kText);
        for (int i = 0; i < count; i++) {
            float cx = cells.x + i * cw + cw * 0.5f;
            s.light(cx, cells.y + 5, std::min(4.0f, cw * 0.3f), (mask >> i) & 1, true);
        }
    }
    return changed;
}

bool Context::color(const char *label, Vec3 &c) {
    Impl &s = *d;
    std::string name = shown(label);
    int saveCols = s.cols;
    columns(1);
    Rect head = s.next(16);
    if (s.visible(head)) {
        s.text(head.x + 2, head.y + 1, name.c_str(), kText, Font::Bold, head.w - 40);
        Rect sw{head.x + head.w - 34, head.y + 1, 34, 14};
        s.box(sw, Color::rgb(c.x, c.y, c.z), true);
    }
    columns(3);
    bool changed = false;
    std::string id = std::string("##") + label;
    changed |= slider(("R" + id + "r").c_str(), c.x, 0, 1, "%.2f");
    changed |= slider(("G" + id + "g").c_str(), c.y, 0, 1, "%.2f");
    changed |= slider(("B" + id + "b").c_str(), c.z, 0, 1, "%.2f");
    columns(saveCols);
    return changed;
}

bool Context::section(const char *label, bool &open) {
    Impl &s = *d;
    int saveCols = s.cols;
    columns(1);
    s.cursorY += 2;
    Rect r = s.next(kRowH);
    uint32_t id = s.idOf(label);
    if (s.behave(id, r, true))
        open = !open;
    if (s.visible(r)) {
        s.box(r, s.hot == id ? Color::rgb(0.48f, 0.48f, 0.57f) : kHeader, false);
        float a = 4, cx = r.x + 10, cy = r.y + r.h * 0.5f;
        if (open)
            s.triangle(cx - a, cy - a * 0.6f, cx + a, cy - a * 0.6f, cx, cy + a * 0.7f, kWhite);
        else
            s.triangle(cx - a * 0.6f, cy - a, cx + a * 0.7f, cy, cx - a * 0.6f, cy + a, kWhite);
        s.textIn(r, 20, shown(label).c_str(), kWhite, Font::Bold);
    }
    columns(saveCols);
    return open;
}

void Context::label(const char *text, Font font) {
    Impl &s = *d;
    Rect r = s.next(s.lineHeight(font) + 2);
    if (s.visible(r))
        s.text(r.x + 2, r.y + 1, text, kText, font, r.w - 4);
}

void Context::message(const char *text) {
    Impl &s = *d;
    int lines = 1;
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    float lh = s.lineHeight(Font::Regular) + 1;
    Rect r = s.next(lines * lh + 6);
    if (!s.visible(r))
        return;
    s.box(r, kDark, true);
    const char *p = text;
    for (int i = 0; i < lines; i++) {
        s.text(r.x + 5, r.y + 3 + i * lh, p, kYellow, Font::Regular, r.w - 10);
        const char *nl = std::strchr(p, '\n');
        if (!nl)
            break;
        p = nl + 1;
    }
}

void Context::table(const std::vector<std::pair<std::string, std::string>> &rows) {
    Impl &s = *d;
    float lh = s.lineHeight(Font::Regular) + 2;
    Rect r = s.next(rows.size() * lh + 6);
    if (!s.visible(r))
        return;
    s.box(r, kDark, true);
    float vx = r.x + r.w * 0.58f;
    for (size_t i = 0; i < rows.size(); i++) {
        float y = r.y + 3 + i * lh;
        if (i % 2)
            s.fill({r.x + 2, y - 1, r.w - 4, lh}, Color::rgb(0.17f, 0.17f, 0.22f));
        s.text(r.x + 6, y, rows[i].first.c_str(), Color::rgb(0.75f, 0.78f, 0.9f), Font::Regular, vx - r.x - 10);
        s.text(vx, y, rows[i].second.c_str(), kYellow, Font::Regular, r.x + r.w - vx - 4);
    }
}

void Context::plot(const float *values, int count, float max, Color curve, const char *overlay, float height) {
    Impl &s = *d;
    Rect r = s.next(height);
    if (!s.visible(r))
        return;
    s.box(r, kDark, true);
    Rect in{r.x + 2, r.y + 14, r.w - 4, r.h - 16};
    // the grid: the quarters of the scale
    for (int i = 1; i < 4; i++)
        s.fill({in.x, std::floor(in.y + in.h * i / 4), in.w, 1}, Color::rgb(0.2f, 0.2f, 0.26f));
    if (count > 1 && max > 0) {
        float dx = in.w / (count - 1);
        for (int i = 0; i < count; i++) {
            float v = std::max(0.0f, std::min(values[i] / max, 1.0f));
            float y = in.y + in.h * (1 - v);
            // a bar per sample, the curve on top
            s.fill({in.x + i * dx, y, std::max(1.0f, dx), in.y + in.h - y},
                   Color{curve.r, curve.g, curve.b, 70});
            s.fill({in.x + i * dx, y - 1, std::max(1.0f, dx), 2}, curve);
        }
    }
    if (overlay)
        s.text(r.x + 5, r.y + 1, overlay, kYellow, Font::Small, r.w - 10);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3g", max);
    float w = s.textWidth(buf, Font::Small);
    s.text(r.x + r.w - w - 5, r.y + 1, buf, Color::rgb(0.6f, 0.6f, 0.7f), Font::Small, 1e9f);
}

bool Context::treeRow(const TreeRow &row, bool &open, bool &visible) {
    Impl &s = *d;
    Rect r = s.next(18);
    uint32_t id = hashOf("tree", (uint32_t)(uintptr_t)row.id * 2654435761u ^ s.panelId);
    float x = r.x + 2 + row.depth * 14;
    Rect arrow{x, r.y, 14, r.h}, eye{x + 14, r.y, 16, r.h}, text{x + 32, r.y, r.x + r.w - x - 32, r.h};
    bool clicked = false;
    if (row.hasChildren && s.behave(id ^ 1, arrow, true))
        open = !open;
    if (s.behave(id ^ 2, eye, true))
        visible = !visible;
    if (s.behave(id ^ 3, text, true))
        clicked = true;
    if (!s.visible(r))
        return clicked;
    if (row.selected)
        s.box(r, kBlue, true);
    else if (s.hot == (id ^ 3))
        s.fill(r, Color::rgb(0.6f, 0.6f, 0.66f));
    // the guides of the depth
    for (int i = 0; i < row.depth; i++)
        s.fill({r.x + 2 + i * 14 + 6, r.y, 1, r.h}, Color::rgb(0.47f, 0.47f, 0.52f));
    Color ink = row.selected ? kWhite : kText;
    if (row.hasChildren) {
        float a = 3.5f, cx = arrow.x + 7, cy = r.y + r.h * 0.5f;
        if (open)
            s.triangle(cx - a, cy - a * 0.6f, cx + a, cy - a * 0.6f, cx, cy + a * 0.7f, ink);
        else
            s.triangle(cx - a * 0.6f, cy - a, cx + a * 0.7f, cy, cx - a * 0.6f, cy + a, ink);
    }
    s.light(eye.x + 7, r.y + r.h * 0.5f, 4, visible, true);
    // the type, in a little colored box
    float tw = s.textWidth(row.tagText, Font::Small) + 8;
    Rect tag{text.x, r.y + 2, tw, r.h - 4};
    s.box(tag, row.tag, false);
    s.textIn(tag, 0, row.tagText, kText, Font::Small, true);
    s.textIn({tag.x + tw + 4, r.y, text.w - tw - 4, r.h}, 0, row.label,
             visible ? ink : row.selected ? Color::rgb(0.75f, 0.78f, 0.9f) : kTextOff);
    return clicked;
}

} // namespace r4::ui
