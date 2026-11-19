// ui.cpp - the GUI of r4::ui, in the look of the panels of IRIS Performer
// (pfuGUI): the colors and the reliefs are those of the port of perfly in
// legacy_opengl_demo.
#include "r4/ui/ui.h"

#include "font_data.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>

namespace r4::ui {

namespace {
// the baked glyphs (font_data.cpp)
const detail::BakedFont &F = detail::bakedFont();
#define kAtlasWidth (F.width)
#define kAtlasHeight (F.height)
#define kFontMetrics (F.metrics)
#define kGlyphBox (F.boxes)
#define kGlyphMetrics (F.glyphs)
using detail::glyphIndex;

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

constexpr float kRowH = 21;
constexpr float kGap = 3;
constexpr float kMargin = 4;
constexpr float kTitleH = 20;

struct Vertex {
    float x;
    float y;
    float u;
    float v;
    Color c;
};

struct Command {
    bool clip = false;
    Rect rect;
    uint32_t first = 0;
    uint32_t count = 0;
    gpu::TextureHandle image; // an image (true colors) instead of the glyphs
};

uint32_t hashOf(const char *s, uint32_t h) {
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
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
    if (c < 0x80) {
        return c;
    }
    if ((c & 0xe0) == 0xc0 && (*s & 0xc0) == 0x80) {
        int code = (c & 0x1f) << 6 | (*s++ & 0x3f);
        return code <= 255 ? code : '?';
    }
    while ((*s & 0xc0) == 0x80) {
        s++;
    }
    return '?';
}

void formatValue(char *buf, size_t n, const char *fmt, float v) {
    std::snprintf(buf, n, fmt, v);
}

} // namespace

struct Context::Impl {
    gpu::Device &device;
    gpu::ShaderHandle shader;
    gpu::PipelineHandle pipeline;
    gpu::PipelineHandle imagePipeline;
    gpu::ShaderHandle imageShader;
    gpu::TextureHandle atlas;
    gpu::BufferHandle vbo;
    gpu::BufferHandle ubo;
    size_t vboSize = 0;
    float whiteU = 0;
    float whiteV = 0;

    int width = 0; // in points
    int height = 0;
    float pixelScale = 1; // pixels per point
    int fontBase = 0;     // the fonts at 1x (0) or 2x (4)
    float fontDiv = 1;    // their pixels per point
    Input in;
    Input prev;
    bool pressed = false;
    bool released = false;
    std::vector<Vertex> verts;
    std::vector<Command> cmds;

    // the widget held by the mouse, the one under it
    uint32_t active = 0;
    uint32_t hot = 0;
    bool activePressedInside = false;

    // the panels: their rectangles (for wantsMouse), their scrolls
    struct PanelState {
        float scroll = 0;
        float contentH = 0;
    };
    std::map<uint32_t, PanelState> panels;
    // the color pickers: open or not, their hue (kept when the color is grey)
    struct Picker {
        bool open = false;
        float hue = 0;
    };
    std::map<uint32_t, Picker> pickers;
    std::vector<Rect> panelRects;
    std::vector<Rect> lastPanelRects;
    bool inPanel = false;
    uint32_t panelId = 0;
    Rect panel;
    Rect clip;
    float cursorY = 0;
    int cols = 1;
    int col = 0;
    float rowY = 0;
    float rowH = 0;
    PanelState *ps = nullptr;

    // the open menu: its widget, the place of the list, its items
    uint32_t menuId = 0;
    Rect menuRect;
    std::vector<std::string> menuItems;
    int menuSelected = -1;
    int menuChoice = -1; // chosen this frame, given at the next call
    uint32_t menuChoiceId = 0;

    explicit Impl(gpu::Device &dev) : device(dev) {
    }

    // --- drawing ---------------------------------------------------------------
    void setClip(bool on, Rect r, gpu::TextureHandle image = {}) {
        if (!cmds.empty() && cmds.back().count == 0) {
            cmds.back().clip = on;
            cmds.back().rect = r;
            cmds.back().image = image;
            return;
        }
        Command c;
        c.clip = on;
        c.rect = r;
        c.first = (uint32_t)verts.size();
        c.image = image;
        cmds.push_back(c);
    }
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c) {
        Vertex a{x0, y0, u0, v0, c};
        Vertex b{x1, y0, u1, v0, c};
        Vertex cc{x1, y1, u1, v1, c};
        Vertex dd{x0, y1, u0, v1, c};
        verts.insert(verts.end(), {a, b, cc, a, cc, dd});
        cmds.back().count += 6;
    }
    void fill(Rect r, Color c) {
        quad(r.x, r.y, r.x + r.w, r.y + r.h, whiteU, whiteV, whiteU, whiteV, c);
    }
    // a rectangle with a color at each corner: top left, top right, bottom right, bottom left
    void gradient(Rect r, Color tl, Color tr, Color br, Color bl) {
        float u = whiteU;
        float v = whiteV;
        Vertex a{r.x, r.y, u, v, tl};
        Vertex b{r.x + r.w, r.y, u, v, tr};
        Vertex c{r.x + r.w, r.y + r.h, u, v, br};
        Vertex e{r.x, r.y + r.h, u, v, bl};
        verts.insert(verts.end(), {a, b, c, a, c, e});
        cmds.back().count += 6;
    }
    void triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
        verts.push_back({x0, y0, whiteU, whiteV, c});
        verts.push_back({x1, y1, whiteU, whiteV, c});
        verts.push_back({x2, y2, whiteU, whiteV, c});
        cmds.back().count += 3;
    }
    // the relief of pfuGUI: light top and left, dark bottom and right
    void bevel(Rect r, bool sunken) {
        Color hi = sunken ? kLow : kHigh;
        Color lo = sunken ? kHigh : kLow;
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
            if (code == '\n') {
                break;
            }
            w += glyphAdvance((int)font, code);
        }
        return w;
    }
    // y: the top of the line; returns the width drawn
    float text(float x, float y, const char *s, Color c, Font font, float maxWidth) {
        int f = (int)font + fontBase;
        float k = 1 / fontDiv;
        float pen = std::floor(x);
        float base = std::floor(y + kFontMetrics[f][1] * k);
        float start = pen;
        while (*s) {
            int code = nextCode(s);
            if (code == '\n') {
                break;
            }
            int g = glyphIndex(code);
            float adv = kGlyphMetrics[f][g][2] * k;
            if (pen + adv - start > maxWidth) {
                break;
            }
            const short *b = kGlyphBox[f][g];
            if (b[2] > b[0]) {
                // on the pixels of the screen
                float gx = std::floor((pen + kGlyphMetrics[f][g][0] * k) * pixelScale + 0.5f) / pixelScale;
                float gy = std::floor((base + kGlyphMetrics[f][g][1] * k) * pixelScale + 0.5f) / pixelScale;
                float w = (b[2] - b[0]) * k;
                float h = (b[3] - b[1]) * k;
                quad(gx, gy, gx + w, gy + h, b[0] / (float)kAtlasWidth, 1 - b[1] / (float)kAtlasHeight, b[2] / (float)kAtlasWidth, 1 - b[3] / (float)kAtlasHeight, c);
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
    void textIn(Rect r, float dx, const char *s, Color c, Font f = Font::REGULAR, bool center = false) {
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
        if (!inPanel) {
            return {};
        }
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
    bool visible(Rect r) const {
        return r.y + r.h > clip.y && r.y < clip.y + clip.h;
    }
    bool hover(Rect r) const {
        return menuId == 0 && inPanel && clip.contains(in.mouseX, in.mouseY) && r.contains(in.mouseX, in.mouseY);
    }
    // the behavior of a button: true when released over it
    bool behave(uint32_t id, Rect r, bool enabled, bool *held = nullptr) {
        bool over = hover(r);
        if (over) {
            hot = id;
        }
        bool click = false;
        if (enabled && over && pressed && active == 0) {
            active = id;
        }
        if (active == id) {
            if (released) {
                click = over;
                active = 0;
            }
        }
        if (held) {
            *held = active == id && over;
        }
        return click;
    }
    uint32_t idOf(const char *label) const {
        return hashOf(label, panelId * 31u + 2166136261u);
    }
};

Context::Context(gpu::Device &device) : d(std::make_unique<Impl>(device)) {
    gpu::Device &dev = device;
    // the atlas, rows from the bottom for the device
    const std::vector<uint8_t> &top = F.pixels;
    std::vector<uint8_t> bottom(top.size());
    for (int y = 0; y < kAtlasHeight; y++) {
        std::memcpy(&bottom[(size_t)y * kAtlasWidth], &top[(size_t)(kAtlasHeight - 1 - y) * kAtlasWidth], kAtlasWidth);
    }
    gpu::TextureDesc td;
    td.width = kAtlasWidth;
    td.height = kAtlasHeight;
    td.format = gpu::TextureFormat::R8;
    td.minFilter = td.magFilter = gpu::Filter::NEAREST;
    td.mipmaps = false;
    td.wrapS = td.wrapT = gpu::Wrap::CLAMP;
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
    vs.attribs = {{0, 2, gpu::AttribType::FLOAT, false, 0}, {1, 2, gpu::AttribType::FLOAT, false, 8}, {2, 4, gpu::AttribType::UBYTE, true, 16}};
    pd.layout.streams = {vs};
    pd.cull = gpu::CullMode::NONE;
    pd.blend = gpu::BlendMode::ALPHA;
    pd.depthTest = false;
    pd.depthWrite = false;
    d->pipeline = dev.createPipeline(pd);
    // the images: their colors, multiplied by the vertex color
    sd.fragment = R"(#version 330
uniform sampler2D atlas;
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor * texture(atlas, vUV); }
)";
    sd.debugName = "r4::ui image";
    d->imageShader = dev.createShader(sd);
    pd.shader = d->imageShader;
    d->imagePipeline = dev.createPipeline(pd);
    float screen[4] = {1, 1, 0, 0};
    d->ubo = dev.createBuffer(gpu::BufferUsage::UNIFORM, screen, sizeof screen, true);
}

Context::~Context() {
    gpu::Device &dev = d->device;
    if (d->vbo) {
        dev.destroyBuffer(d->vbo);
    }
    dev.destroyBuffer(d->ubo);
    dev.destroyPipeline(d->pipeline);
    dev.destroyShader(d->shader);
    dev.destroyPipeline(d->imagePipeline);
    dev.destroyShader(d->imageShader);
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
    if (!input.left && !input.right && !s.released) {
        s.active = 0;
    }
}

void Context::endFrame() {
    Impl &s = *d;
    if (!s.menuId) {
        return;
    }
    // the list of the open menu, over everything
    s.setClip(false, {});
    float ih = s.menuRect.h / std::max<size_t>(1, s.menuItems.size());
    for (size_t i = 0; i < s.menuItems.size(); i++) {
        Rect r{s.menuRect.x, s.menuRect.y + i * ih, s.menuRect.w, ih};
        bool over = r.contains(s.in.mouseX, s.in.mouseY);
        s.box(r, over ? kBlue : kMenuFace, false);
        if ((int)i == s.menuSelected) {
            s.light(r.x + 8, r.y + ih * 0.5f, 3, true, true);
        }
        s.textIn(r, 16, s.menuItems[i].c_str(), over ? kWhite : kText);
    }
}

void Context::render() {
    Impl &s = *d;
    gpu::Device &dev = s.device;
    if (s.verts.empty() || !s.pipeline) {
        return;
    }
    size_t bytes = s.verts.size() * sizeof(Vertex);
    if (bytes > s.vboSize) {
        if (s.vbo) {
            dev.destroyBuffer(s.vbo);
        }
        s.vboSize = std::max(bytes * 2, (size_t)65536);
        s.vbo = dev.createBuffer(gpu::BufferUsage::VERTEX, nullptr, s.vboSize, true);
    }
    dev.updateBuffer(s.vbo, 0, s.verts.data(), bytes);
    float screen[4] = {(float)s.width, (float)s.height, 0, 0};
    dev.updateBuffer(s.ubo, 0, screen, sizeof screen);
    gpu::ClearValues none;
    none.color = none.depth = false;
    // the points of the layout on the pixels of the framebuffer
    float k = s.pixelScale;
    int pw = (int)std::lround(s.width * k);
    int ph = (int)std::lround(s.height * k);
    dev.beginPass(0, 0, pw, ph, none);
    dev.bindPipeline(s.pipeline);
    dev.bindVertexBuffer(0, s.vbo);
    dev.bindUniformBuffer(0, s.ubo, 0, sizeof screen);
    dev.bindTexture(0, s.atlas);
    bool imageBound = false;
    for (const Command &c : s.cmds) {
        if (!c.count) {
            continue;
        }
        if ((bool)c.image != imageBound || c.image) {
            dev.bindPipeline(c.image ? s.imagePipeline : s.pipeline);
            dev.bindTexture(0, c.image ? c.image : s.atlas);
            imageBound = (bool)c.image;
        }
        if (c.clip) {
            int x0 = (int)std::floor(c.rect.x * k);
            int y0 = (int)std::floor(c.rect.y * k);
            int x1 = (int)std::ceil((c.rect.x + c.rect.w) * k);
            int y1 = (int)std::ceil((c.rect.y + c.rect.h) * k);
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
    if (s.menuId || s.active) {
        return true;
    }
    for (const Rect &r : s.panelRects.empty() ? s.lastPanelRects : s.panelRects) {
        if (r.contains(s.in.mouseX, s.in.mouseY)) {
            return true;
        }
    }
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
        s.textIn(t, 6, title, kYellow, Font::BOLD);
        top = t.y + t.h + 1;
    }
    s.clip = {r.x + 1, top, r.w - 2, r.y + r.h - 1 - top};
    // the wheel scrolls the panel under the mouse
    if (s.menuId == 0 && s.in.wheel != 0 && s.clip.contains(s.in.mouseX, s.in.mouseY)) {
        s.ps->scroll -= s.in.wheel * 40;
    }
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
    if (s.col) {
        s.col = 0;
    }
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

float Context::remainingHeight() const {
    return d->clip.y + d->clip.h - d->cursorY - kMargin;
}

Rect Context::next(float height) {
    return d->next(height);
}

void Context::image(gpu::TextureHandle texture, float aspect, float maxHeight) {
    Impl &s = *d;
    float w = s.panel.w - 2 * 4.0f; // the width of the panel
    float h = aspect > 0 ? w / aspect : w;
    if (maxHeight > 0 && h > maxHeight) {
        h = maxHeight;
        w = aspect > 0 ? h * aspect : h;
    }
    Rect r = s.next(h + 4);
    if (!s.visible(r) || !texture) {
        return;
    }
    s.box(r, Color::rgb(0.12f, 0.12f, 0.16f), true);
    Rect in{r.x + (r.w - w) * 0.5f, r.y + 2, w, h};
    image(texture, in);
}

void Context::image(gpu::TextureHandle texture, Rect r) {
    Impl &s = *d;
    Command back = s.cmds.back();
    // its own draw, then the glyphs again with the same clip
    s.setClip(back.clip, back.rect, texture);
    s.quad(r.x, r.y, r.x + r.w, r.y + r.h, 0, 1, 1, 0, Color::rgb(1, 1, 1));
    s.setClip(back.clip, back.rect);
}

void Context::beginCanvas(const char *id, Rect r) {
    Impl &s = *d;
    s.panelId = hashOf(id, 2166136261u);
    s.clip = r;
    s.setClip(true, r);
    s.inPanel = true;
}

void Context::endCanvas() {
    d->inPanel = false;
    d->setClip(false, {});
}

bool Context::clickable(const void *id, Rect r) {
    Impl &s = *d;
    uint32_t h = hashOf("click", (uint32_t)(uintptr_t)id * 2654435761u ^ s.panelId);
    if (s.hover(r)) {
        s.panelRects.push_back(r); // the mouse is the GUI's there
    }
    return s.behave(h, r, true);
}

bool Context::hovered(Rect r) const {
    return d->hover(r);
}

void Context::line(float x0, float y0, float x1, float y1, Color c, float width) {
    Impl &s = *d;
    float dx = x1 - x0;
    float dy = y1 - y0;
    float l = std::sqrt(dx * dx + dy * dy);
    if (l <= 0) {
        return;
    }
    float nx = -dy / l * width * 0.5f;
    float ny = dx / l * width * 0.5f;
    s.triangle(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    s.triangle(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}
void Context::fill(Rect r, Color c) {
    d->fill(r, c);
}
void Context::bevel(Rect r, bool sunken) {
    d->bevel(r, sunken);
}
float Context::text(float x, float y, const char *s, Color c, Font font, float maxWidth) {
    return d->text(x, y, s, c, font, maxWidth);
}
float Context::textWidth(const char *s, Font font) const {
    return d->textWidth(s, font);
}

// --- widgets ----------------------------------------------------------------------

bool Context::button(const char *label, bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH);
    uint32_t id = s.idOf(label);
    bool held = false;
    bool click = s.behave(id, r, enabled, &held);
    if (s.visible(r)) {
        s.box(r, held ? kFacePressed : s.hot == id && enabled ? kFaceHot : kFace, held);
        s.textIn(r, 0, shown(label).c_str(), enabled ? kText : kTextOff, Font::REGULAR, true);
    }
    return click;
}

bool Context::toggle(const char *label, bool &on, bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH);
    uint32_t id = s.idOf(label);
    bool held = false;
    bool click = s.behave(id, r, enabled, &held);
    if (click) {
        on = !on;
    }
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
    if (over) {
        s.hot = id;
    }
    if (enabled && over && s.pressed && s.active == 0 && !items.empty()) {
        // the list under the button, or above it when there is no room
        float ih = std::max(18.0f, r.h * 0.9f);
        float h = ih * items.size();
        float y = r.y + r.h;
        if (y + h > s.height) {
            y = std::max(0.0f, r.y - h);
        }
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
        if (selected >= 0 && selected < (int)items.size()) {
            t += (t.empty() ? "" : ": ") + items[selected];
        }
        float a = std::floor(r.h * 0.2f);
        s.textIn({r.x, r.y, r.w - 2 * a - 6, r.h}, 5, t.c_str(), enabled ? kText : kTextOff);
        float cx = r.x + r.w - a - 6;
        float cy = r.y + r.h * 0.5f;
        s.triangle(cx - a, cy - a * 0.5f, cx + a, cy - a * 0.5f, cx, cy + a * 0.6f, enabled ? kText : kTextOff);
    }
    return changed;
}

bool Context::slider(const char *label, float &value, float min, float max, const char *format, bool log, bool enabled) {
    Impl &s = *d;
    Rect r = s.next(kRowH + 13);
    uint32_t id = s.idOf(label);
    Rect track{r.x + 5, r.y + r.h - 12, r.w - 10, 7};
    float lo = min;
    float hi = max;
    if (log && lo <= 0) {
        lo = hi > 0 ? hi * 1e-4f : 1e-4f;
    }
    auto toT = [&](float v) {
        float t;
        if (log) {
            t = hi > lo && v > 0 ? std::log(std::max(v, lo) / lo) / std::log(hi / lo) : 0;
        } else {
            t = hi > lo ? (v - lo) / (hi - lo) : 0;
        }
        return std::max(0.0f, std::min(1.0f, t));
    };
    auto fromT = [&](float t) {
        t = std::max(0.0f, std::min(1.0f, t));
        return log ? lo * std::pow(hi / lo, t) : lo + t * (hi - lo);
    };
    bool over = s.hover(r);
    if (over) {
        s.hot = id;
    }
    bool changed = false;
    if (enabled && over && s.pressed && s.active == 0) {
        s.active = id;
    }
    if (s.active == id && (s.in.left || s.in.right || s.pressed)) {
        float v = fromT((s.in.mouseX - track.x) / track.w);
        if (v != value) {
            value = v;
            changed = true;
        }
    }
    if (s.visible(r)) {
        s.box(r, kSliderFace, false);
        char val[64];
        char buf[256];
        formatValue(val, sizeof val, format, value);
        std::snprintf(buf, sizeof buf, "%s: %s", shown(label).c_str(), val);
        s.textIn({r.x, r.y + 1, r.w, r.h - 12}, 5, buf, enabled ? kText : kTextOff);
        float t = toT(value);
        s.box(track, kTrack, true);
        if (enabled) {
            s.fill({track.x + 1, track.y + 1, std::floor(t * (track.w - 2)), track.h - 2}, kBlue);
        }
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
    float lw = name.empty() ? 0 : std::min(r.w * 0.3f, s.textWidth(name.c_str(), Font::REGULAR) + 10);
    int n = (int)items.size();
    float cw = n ? (r.w - lw) / n : 0;
    bool changed = false;
    bool over = s.hover(r);
    if (over) {
        s.hot = id;
    }
    if (enabled && over && s.pressed && s.active == 0 && n && s.in.mouseX >= r.x + lw) {
        int i = std::min(n - 1, (int)((s.in.mouseX - r.x - lw) / cw));
        changed = i != selected;
        selected = i;
    }
    if (s.visible(r)) {
        s.box(r, kFace, false);
        if (lw > 0) {
            s.textIn({r.x, r.y, lw, r.h}, 5, name.c_str(), enabled ? kText : kTextOff);
        }
        float rad = std::floor(r.h * 0.16f);
        for (int i = 0; i < n; i++) {
            Rect c{r.x + lw + i * cw, r.y, cw, r.h};
            if (i == selected) {
                s.fill({c.x + 2, c.y + 2, c.w - 4, c.h - 4}, kFacePressed);
            }
            s.light(c.x + rad + 5, c.y + c.h * 0.5f, rad, i == selected, enabled);
            s.textIn({c.x + 2 * rad + 8, c.y, c.w - 2 * rad - 8, c.h}, 0, items[i].c_str(), enabled ? kText : kTextOff);
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
    if (over) {
        s.hot = id;
    }
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

namespace {

Vec3 hsvToRgb(float h, float sat, float val) {
    float hh = std::fmod(std::max(h, 0.0f), 1.0f) * 6;
    int i = (int)hh;
    float f = hh - i;
    float p = val * (1 - sat);
    float q = val * (1 - sat * f);
    float t = val * (1 - sat * (1 - f));
    switch (i) {
    case 0:
        return {val, t, p};
    case 1:
        return {q, val, p};
    case 2:
        return {p, val, t};
    case 3:
        return {p, q, val};
    case 4:
        return {t, p, val};
    default:
        return {val, p, q};
    }
}

// hue, saturation, value; the hue is kept when the color has none
Vec3 rgbToHsv(Vec3 c, float keepHue) {
    float mx = std::max(c.x, std::max(c.y, c.z));
    float mn = std::min(c.x, std::min(c.y, c.z));
    float d = mx - mn;
    float h = keepHue;
    if (d > 1e-5f) {
        if (mx == c.x) {
            h = std::fmod((c.y - c.z) / d + 6, 6.0f) / 6;
        } else if (mx == c.y) {
            h = ((c.z - c.x) / d + 2) / 6;
        } else {
            h = ((c.x - c.y) / d + 4) / 6;
        }
    }
    return {h, mx > 0 ? d / mx : 0, mx};
}

} // namespace

bool Context::color(const char *label, Vec3 &c) {
    Impl &s = *d;
    std::string name = shown(label);
    uint32_t id = s.idOf(label);
    Impl::Picker &pk = s.pickers[id];
    int saveCols = s.cols;
    columns(1);
    bool changed = false;

    // the button: the name, the swatch, the arrow; it opens the picker
    Rect r = s.next(kRowH);
    bool held = false;
    if (s.behave(id, r, true, &held)) {
        pk.open = !pk.open;
    }
    if (s.visible(r)) {
        s.box(r, held || pk.open ? kFacePressed : s.hot == id ? kFaceHot : kFace, held || pk.open);
        s.textIn({r.x, r.y, r.w - 70, r.h}, 5, name.c_str(), kText);
        Rect sw{r.x + r.w - 62, r.y + 4, 40, r.h - 8};
        s.box(sw, Color::rgb(c.x, c.y, c.z), true);
        float a = std::floor(r.h * 0.2f);
        float cx = r.x + r.w - a - 6;
        float cy = r.y + r.h * 0.5f;
        if (pk.open) {
            s.triangle(cx - a, cy - a * 0.5f, cx + a, cy - a * 0.5f, cx, cy + a * 0.6f, kText);
        } else {
            s.triangle(cx - a * 0.5f, cy - a, cx + a * 0.6f, cy, cx - a * 0.5f, cy + a, kText);
        }
    }
    if (pk.open) {
        Vec3 hsv = rgbToHsv(c, pk.hue);
        pk.hue = hsv.x;
        // the square: saturation across, value up; the bar: the hue
        Rect area = s.next(112);
        Rect sq{area.x + 2, area.y + 2, area.w - 28, area.h - 4};
        Rect bar{area.x + area.w - 22, area.y + 2, 20, area.h - 4};
        uint32_t sqId = id ^ 0x5157u;
        uint32_t barId = id ^ 0xbab0u;
        for (auto [rid, rr] : {std::pair<uint32_t, Rect>{sqId, sq}, std::pair<uint32_t, Rect>{barId, bar}}) {
            if (s.hover(rr)) {
                s.hot = rid;
                if (s.pressed && s.active == 0) {
                    s.active = rid;
                }
            }
        }
        if ((s.in.left || s.in.right || s.pressed) && (s.active == sqId || s.active == barId)) {
            auto clamp01 = [](float v) {
                return std::max(0.0f, std::min(1.0f, v));
            };
            if (s.active == sqId) {
                hsv.y = clamp01((s.in.mouseX - sq.x) / sq.w);
                hsv.z = clamp01(1 - (s.in.mouseY - sq.y) / sq.h);
            } else {
                hsv.x = pk.hue = clamp01((s.in.mouseY - bar.y) / bar.h) * 0.9999f;
            }
            Vec3 n = hsvToRgb(hsv.x, hsv.y, hsv.z);
            changed = n != c;
            c = n;
        }
        if (s.visible(area)) {
            s.box(area, kDark, true);
            Vec3 pure = hsvToRgb(hsv.x, 1, 1);
            Color hue = Color::rgb(pure.x, pure.y, pure.z);
            Color black0{0, 0, 0, 0};
            Color black{0, 0, 0, 255};
            s.gradient(sq, kWhite, hue, hue, kWhite);
            s.gradient(sq, black0, black0, black, black);
            for (int i = 0; i < 6; i++) {
                Vec3 a = hsvToRgb(i / 6.0f, 1, 1);
                Vec3 b = hsvToRgb((i + 1) / 6.0f - 1e-4f, 1, 1);
                Color ca = Color::rgb(a.x, a.y, a.z);
                Color cb = Color::rgb(b.x, b.y, b.z);
                s.gradient({bar.x, bar.y + bar.h * i / 6, bar.w, bar.h / 6}, ca, ca, cb, cb);
            }
            // the marks: a ring on the square, a notch on the bar
            float mx = std::floor(sq.x + hsv.y * sq.w);
            float my = std::floor(sq.y + (1 - hsv.z) * sq.h);
            Color ring = hsv.z > 0.5f ? kText : kWhite;
            s.fill({mx - 4, my - 4, 9, 1}, ring);
            s.fill({mx - 4, my + 4, 9, 1}, ring);
            s.fill({mx - 4, my - 4, 1, 9}, ring);
            s.fill({mx + 4, my - 4, 1, 9}, ring);
            float hy = std::floor(bar.y + hsv.x * bar.h);
            s.box({bar.x - 2, hy - 2, bar.w + 4, 5}, kKnob, false);
        }
        // the exact values
        columns(3);
        std::string sid = std::string("##") + label;
        changed |= slider(("R" + sid + "r").c_str(), c.x, 0, 1, "%.2f");
        changed |= slider(("G" + sid + "g").c_str(), c.y, 0, 1, "%.2f");
        changed |= slider(("B" + sid + "b").c_str(), c.z, 0, 1, "%.2f");
        columns(1);
        char hex[32];
        std::snprintf(hex, sizeof hex, "#%02X%02X%02X", (int)std::lround(c.x * 255), (int)std::lround(c.y * 255), (int)std::lround(c.z * 255));
        message(hex);
    }
    columns(saveCols);
    return changed;
}

bool Context::section(const char *label, bool &open) {
    Impl &s = *d;
    int saveCols = s.cols;
    columns(1);
    s.cursorY += 2;
    Rect r = s.next(kRowH);
    // its own ids: a section often holds a widget of its name ("Lighting",
    // the toggle of the lighting), which would take its clicks
    uint32_t id = hashOf("section", s.idOf(label));
    if (s.behave(id, r, true)) {
        open = !open;
    }
    if (s.visible(r)) {
        s.box(r, s.hot == id ? Color::rgb(0.48f, 0.48f, 0.57f) : kHeader, false);
        float a = 4;
        float cx = r.x + 10;
        float cy = r.y + r.h * 0.5f;
        if (open) {
            s.triangle(cx - a, cy - a * 0.6f, cx + a, cy - a * 0.6f, cx, cy + a * 0.7f, kWhite);
        } else {
            s.triangle(cx - a * 0.6f, cy - a, cx + a * 0.7f, cy, cx - a * 0.6f, cy + a, kWhite);
        }
        s.textIn(r, 20, shown(label).c_str(), kWhite, Font::BOLD);
    }
    columns(saveCols);
    return open;
}

void Context::label(const char *text, Font font) {
    Impl &s = *d;
    Rect r = s.next(s.lineHeight(font) + 2);
    if (s.visible(r)) {
        s.text(r.x + 2, r.y + 1, text, kText, font, r.w - 4);
    }
}

void Context::message(const char *text) {
    Impl &s = *d;
    int lines = 1;
    for (const char *p = text; *p; p++) {
        lines += *p == '\n';
    }
    float lh = s.lineHeight(Font::REGULAR) + 1;
    Rect r = s.next(lines * lh + 6);
    if (!s.visible(r)) {
        return;
    }
    s.box(r, kDark, true);
    const char *p = text;
    for (int i = 0; i < lines; i++) {
        s.text(r.x + 5, r.y + 3 + i * lh, p, kYellow, Font::REGULAR, r.w - 10);
        const char *nl = std::strchr(p, '\n');
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
}

void Context::table(const std::vector<std::pair<std::string, std::string>> &rows) {
    Impl &s = *d;
    float lh = s.lineHeight(Font::REGULAR) + 2;
    Rect r = s.next(rows.size() * lh + 6);
    if (!s.visible(r)) {
        return;
    }
    s.box(r, kDark, true);
    float vx = r.x + r.w * 0.58f;
    for (size_t i = 0; i < rows.size(); i++) {
        float y = r.y + 3 + i * lh;
        if (i % 2) {
            s.fill({r.x + 2, y - 1, r.w - 4, lh}, Color::rgb(0.17f, 0.17f, 0.22f));
        }
        s.text(r.x + 6, y, rows[i].first.c_str(), Color::rgb(0.75f, 0.78f, 0.9f), Font::REGULAR, vx - r.x - 10);
        s.text(vx, y, rows[i].second.c_str(), kYellow, Font::REGULAR, r.x + r.w - vx - 4);
    }
}

void Context::plot(const float *values, int count, float max, Color curve, const char *overlay, float height) {
    Impl &s = *d;
    Rect r = s.next(height);
    if (!s.visible(r)) {
        return;
    }
    s.box(r, kDark, true);
    Rect in{r.x + 2, r.y + 14, r.w - 4, r.h - 16};
    // the grid: the quarters of the scale
    for (int i = 1; i < 4; i++) {
        s.fill({in.x, std::floor(in.y + in.h * i / 4), in.w, 1}, Color::rgb(0.2f, 0.2f, 0.26f));
    }
    if (count > 1 && max > 0) {
        float dx = in.w / (count - 1);
        for (int i = 0; i < count; i++) {
            float v = std::max(0.0f, std::min(values[i] / max, 1.0f));
            float y = in.y + in.h * (1 - v);
            // a bar per sample, the curve on top
            s.fill({in.x + i * dx, y, std::max(1.0f, dx), in.y + in.h - y}, Color{curve.r, curve.g, curve.b, 70});
            s.fill({in.x + i * dx, y - 1, std::max(1.0f, dx), 2}, curve);
        }
    }
    if (overlay) {
        s.text(r.x + 5, r.y + 1, overlay, kYellow, Font::SMALL, r.w - 10);
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3g", max);
    float w = s.textWidth(buf, Font::SMALL);
    s.text(r.x + r.w - w - 5, r.y + 1, buf, Color::rgb(0.6f, 0.6f, 0.7f), Font::SMALL, 1e9f);
}

bool Context::treeRow(const TreeRow &row, bool &open, bool &visible) {
    Impl &s = *d;
    Rect r = s.next(18);
    uint32_t id = hashOf("tree", (uint32_t)(uintptr_t)row.id * 2654435761u ^ s.panelId);
    float x = r.x + 2 + row.depth * 14;
    Rect arrow{x, r.y, 14, r.h};
    Rect eye{x + 14, r.y, 16, r.h};
    Rect text{x + 32, r.y, r.x + r.w - x - 32, r.h};
    bool clicked = false;
    if (row.hasChildren && s.behave(id ^ 1, arrow, true)) {
        open = !open;
    }
    if (s.behave(id ^ 2, eye, true)) {
        visible = !visible;
    }
    if (s.behave(id ^ 3, text, true)) {
        clicked = true;
    }
    if (!s.visible(r)) {
        return clicked;
    }
    if (row.selected) {
        s.box(r, kBlue, true);
    } else if (s.hot == (id ^ 3)) {
        s.fill(r, Color::rgb(0.6f, 0.6f, 0.66f));
    }
    // the guides of the depth
    for (int i = 0; i < row.depth; i++) {
        s.fill({r.x + 2 + i * 14 + 6, r.y, 1, r.h}, Color::rgb(0.47f, 0.47f, 0.52f));
    }
    Color ink = row.selected ? kWhite : kText;
    if (row.hasChildren) {
        float a = 3.5f;
        float cx = arrow.x + 7;
        float cy = r.y + r.h * 0.5f;
        if (open) {
            s.triangle(cx - a, cy - a * 0.6f, cx + a, cy - a * 0.6f, cx, cy + a * 0.7f, ink);
        } else {
            s.triangle(cx - a * 0.6f, cy - a, cx + a * 0.7f, cy, cx - a * 0.6f, cy + a, ink);
        }
    }
    s.light(eye.x + 7, r.y + r.h * 0.5f, 4, visible, true);
    // the type, in a little colored box
    float tw = s.textWidth(row.tagText, Font::SMALL) + 8;
    Rect tag{text.x, r.y + 2, tw, r.h - 4};
    s.box(tag, row.tag, false);
    s.textIn(tag, 0, row.tagText, kText, Font::SMALL, true);
    s.textIn({tag.x + tw + 4, r.y, text.w - tw - 4, r.h}, 0, row.label, visible ? ink : row.selected ? Color::rgb(0.75f, 0.78f, 0.9f) : kTextOff);
    return clicked;
}

} // namespace r4::ui
