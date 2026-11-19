// cockpit - r4::raster and the post process: a cockpit drawn on the CPU over
// the scene, as the old games did, a HUD in true colors at the size of the
// window, a screen of the cockpit on a mesh of the scene, and the effects on
// the whole frame.
//
// The cockpit is a surface of 320x200 indexed pixels (or more, or in true
// colors: the panel changes it), shown by an Overlay that applies its
// palette: fades, tints and the cycling of colors cost nothing. The HUD is
// an RGBA surface of the size of the window, blended. The radar is a small
// indexed surface, drawn into a texture for a panel standing in the scene.
#include "common/example.h"
#include "common/shapes.h"

#include "r4/raster/raster.h"
#include "r4/render/overlay.h"
#include "r4/render/postprocess.h"
#include "r4/render/renderer.h"
#include "r4/scene/node.h"
#include "r4/ui/controls.h"
#include "r4/ui/ui.h"

#include <random>

using namespace r4;
using raster::Surface;

namespace {

// the indices of the VGA palette used by the cockpit
enum : uint8_t {
    kBlack = 0,
    kBlue = 1,
    kGreen = 2,
    kCyan = 3,
    kRed = 4,
    kBrown = 6,
    kLightGrey = 7,
    kDarkGrey = 8,
    kLightGreen = 10,
    kYellow = 14,
    kWhite = 15,
    kGrey1 = 18,
    kGrey2 = 21,
    kGrey3 = 24,
    kSky = 53,
    kGround = 114,
    kClear = 255,
};

// the cockpit at any size: its drawings are made for 320 x 200 and scaled
void drawCockpit(Surface &s, const raster::Palette &palette, float heading, float pitch, float roll, float time, float speed) {
    bool indexed = s.format() == raster::Format::Indexed8;
    // a color: an index, or its true color
    auto C = [&](uint8_t i) {
        return indexed ? (uint32_t)i : palette.colors[i];
    };
    float k = s.width() / 320.0f;
    auto X = [&](float v) {
        return (int)std::lround(v * k);
    };
    s.clear(indexed ? (uint32_t)kClear : raster::rgba(0, 0, 0, 0));
    const raster::Font &font = raster::Font::builtin((int)std::lround(11 * k));

    // the frame of the canopy, and the dashboard
    s.fillRect(0, X(140), s.width(), s.height() - X(140), C(kGrey1));
    s.hline(0, s.width(), X(140), C(kGrey3));
    s.fillTriangle(0, X(140), X(40), X(140), 0, X(40), C(kGrey1));
    s.fillTriangle(s.width(), X(140), X(280), X(140), s.width(), X(40), C(kGrey1));
    s.lineThick(X(40), X(140), 0, X(40), C(kGrey3), std::max(1, X(1)));
    s.lineThick(X(280), X(140), s.width(), X(40), C(kGrey3), std::max(1, X(1)));

    // the radar: a sweep turning, dashed rings, blips
    int rx = X(48);
    int ry = X(172);
    int rr = X(24);
    s.fillCircle(rx, ry, rr, C(kBlack));
    s.circle(rx, ry, rr, C(kLightGreen));
    s.circle(rx, ry, rr / 2, C(kGreen));
    float sweep = time * 2.5f;
    s.line(rx, ry, rx + (int)(std::cos(sweep) * rr), ry + (int)(std::sin(sweep) * rr), C(kLightGreen));
    s.lineDashed(rx - rr, ry, rx + rr, ry, C(kGreen), std::max(1, X(2)), std::max(1, X(2)));
    for (int i = 0; i < 4; i++) {
        float a = i * 1.7f + 0.3f;
        float d = 0.35f + 0.15f * i;
        s.fillRect(rx + (int)(std::cos(a) * rr * d) - X(1), ry + (int)(std::sin(a) * rr * d) - X(1), X(2) + 1, X(2) + 1, C(kYellow));
    }

    // the horizon: a ball turned by the roll, moved by the pitch
    int hx = X(160);
    int hy = X(170);
    int hw = X(36);
    int hh = X(24);
    s.setClip(hx - hw, hy - hh, 2 * hw, 2 * hh);
    s.fillRect(hx - hw, hy - hh, 2 * hw, 2 * hh, C(kSky));
    float r = radians(roll);
    float off = pitch / 45.0f * hh;
    float dx = std::cos(r);
    float dy = std::sin(r);
    float cx = hx - dy * off;
    float cy = hy + dx * off;
    int L = 4 * hw;
    int ground[8] = {(int)(cx - dx * L), (int)(cy - dy * L), (int)(cx + dx * L), (int)(cy + dy * L), (int)(cx + dx * L - dy * L), (int)(cy + dy * L + dx * L), (int)(cx - dx * L - dy * L), (int)(cy - dy * L + dx * L)};
    s.fillPolygon(ground, 4, C(kGround));
    s.line((int)(cx - dx * L), (int)(cy - dy * L), (int)(cx + dx * L), (int)(cy + dy * L), C(kWhite));
    s.resetClip();
    s.hline(hx - X(10), hx - X(3), hy, C(kYellow));
    s.hline(hx + X(3), hx + X(10), hy, C(kYellow));
    s.rect(hx - hw, hy - hh, 2 * hw, 2 * hh, C(kGrey3));

    // the compass tape: the ticks of the heading
    int tx = X(232);
    int ty = X(150);
    int tw = X(76);
    int th = X(14);
    s.fillRect(tx, ty, tw, th, C(kBlack));
    s.setClip(tx, ty, tw, th);
    for (int deg = -60; deg <= 60; deg += 5) {
        float h = std::floor(heading / 5) * 5 + deg;
        int x = tx + tw / 2 + (int)((h - heading) * k);
        int major = ((int)h % 30 + 30) % 30 == 0;
        s.vline(x, ty + th - (major ? X(6) : X(3)), ty + th, C(kLightGreen));
        if (major) {
            int n = ((int)h % 360 + 360) % 360;
            s.text(font, x, ty, std::to_string(n / 10), C(kLightGreen), Surface::Align::Center);
        }
    }
    s.resetClip();
    s.vline(tx + tw / 2, ty, ty + th, C(kYellow));

    // the readouts
    char buf[64];
    std::snprintf(buf, sizeof buf, "SPD %3.0f", speed);
    s.text(font, X(232), X(170), buf, C(kLightGreen));
    std::snprintf(buf, sizeof buf, "HDG %03.0f", std::fmod(heading + 360, 360.0f));
    s.text(font, X(232), X(182), buf, C(kLightGreen));
    s.text(font, X(110), X(144), "R4NKINE", C(kRed));
}

// the HUD: true colors at the size of the window, blended over the scene
void drawHud(Surface &s, float pitch, float roll) {
    s.clear(raster::rgba(0, 0, 0, 0));
    s.blend = true;
    uint32_t green = raster::rgba(80, 255, 120, 220);
    uint32_t dim = raster::rgba(80, 255, 120, 110);
    int cx = s.width() / 2;
    int cy = s.height() * 2 / 5;
    float scale = s.height() / 400.0f;
    int t = std::max(1, (int)std::lround(scale * 1.5f));
    s.lineThick(cx - (int)(30 * scale), cy, cx - (int)(10 * scale), cy, green, t);
    s.lineThick(cx + (int)(10 * scale), cy, cx + (int)(30 * scale), cy, green, t);
    s.circle(cx, cy, (int)(4 * scale), green);
    // the pitch ladder, turned by the roll
    float r = radians(-roll);
    float c = std::cos(r);
    float sn = std::sin(r);
    for (int deg = -20; deg <= 20; deg += 10) {
        float y = (deg - pitch) * 6 * scale;
        auto P = [&](float x, float yy, int &ox, int &oy) {
            ox = cx + (int)(x * c - yy * sn);
            oy = cy + (int)(x * sn + yy * c);
        };
        int x0;
        int y0;
        int x1;
        int y1;
        P(-70 * scale, y, x0, y0);
        P(-25 * scale, y, x1, y1);
        if (deg) {
            s.lineDashed(x0, y0, x1, y1, dim, deg < 0 ? 4 : 1, deg < 0 ? 3 : 0);
        } else {
            s.lineThick(x0, y0, x1, y1, green, t);
        }
        P(25 * scale, y, x0, y0);
        P(70 * scale, y, x1, y1);
        if (deg) {
            s.lineDashed(x0, y0, x1, y1, dim, deg < 0 ? 4 : 1, deg < 0 ? 3 : 0);
        } else {
            s.lineThick(x0, y0, x1, y1, green, t);
        }
    }
    s.blend = false;
}

// the radar alone, for the screen standing in the scene
void drawRadar(Surface &s, float time) {
    int n = s.width();
    int c = n / 2;
    s.clear(kBlack);
    s.circle(c, c, c - 2, kLightGreen);
    s.circle(c, c, c / 2, kGreen);
    s.lineDashed(2, c, n - 2, c, kGreen, 2, 3);
    s.lineDashed(c, 2, c, n - 2, kGreen, 2, 3);
    float sweep = time * 2.5f;
    for (int i = 0; i < 12; i++) {
        float a = sweep - i * 0.05f;
        s.line(c, c, c + (int)(std::cos(a) * (c - 3)), c + (int)(std::sin(a) * (c - 3)), i < 2 ? kLightGreen : kGreen);
    }
    s.text(raster::Font::builtin(11), 4, 2, "RADAR", kYellow);
}

} // namespace

int main(int argc, char **argv) {
    example::Window win;
    if (!win.open("cockpit", argc, argv, 1280, 800)) {
        return 1;
    }
    render::Renderer renderer(*win.device);
    renderer.environment().clearColor = {0.45f, 0.6f, 0.85f, 1};
    renderer.environment().lightDirection = normalize(Vec3{0.3f, 0.6f, -0.7f});
    render::PostProcess post(*win.device);
    render::Overlay cockpitOverlay(*win.device);
    render::Overlay hudOverlay(*win.device);
    render::Overlay radarOverlay(*win.device);

    // a ground and a few trees
    auto root = scene::make<scene::Group>();
    auto ground = scene::make<scene::Geode>();
    ground->addDrawable(example::drawable(renderer, example::ground(600), {0.35f, 0.45f, 0.28f, 1}));
    root->addChild(ground);
    auto trees = scene::make<scene::Instancer>();
    trees->addLevel({{example::drawable(renderer, example::cylinder(1.6f, 0, 0, 5.6f, 8), {0.2f, 0.5f, 0.2f, 1}), Mat4(1.0f)}});
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> place(-280, 280);
    for (int i = 0; i < 3000; i++) {
        trees->addInstance(translate({place(rng), place(rng), 0}));
    }
    root->addChild(trees);

    // the screen of the radar, standing in the scene
    assets::Mesh screenMesh;
    screenMesh.name = "radar screen";
    for (int i = 0; i < 4; i++) {
        assets::Vertex v;
        float u = (float)(i & 1);
        float w = (float)(i >> 1);
        v.position = {(u - 0.5f) * 8, 0, 4 + w * 8};
        v.normal = {0, -1, 0};
        v.uv = {u, w};
        screenMesh.vertices.push_back(v);
    }
    screenMesh.indices = {0, 1, 3, 0, 3, 2};
    screenMesh.computeBounds();
    auto screenGpu = renderer.uploadMesh(screenMesh);
    auto screen = scene::make<scene::Geode>();
    screen->setMatrix(translate({0, 12, 0}));
    root->addChild(screen);
    gpu::TextureHandle screenTexture;

    // the surfaces
    raster::Palette vga = raster::Palette::vga();
    int resolution = 0;
    int format = 0;
    int effect = 0;
    Surface cockpit(320, 200);
    Surface hud;
    Surface radar(128, 128);
    radar.transparentIndex = -1;
    bool showCockpit = true;
    bool showHud = true;
    bool openCockpit = true;
    bool openPost = false;
    float fadeAmount = 0;

    win.distance = 30;
    win.orbit.setAngles(0, 8, 0);
    render::Camera camera;
    float time = 0;
    while (win.poll()) {
        time += 1 / 60.0f;
        float heading;
        float pitch;
        float roll;
        win.orbit.angles(heading, pitch, roll);

        // the panel
        ui::Context &gui = win.beginGui();
        gui.beginPanel("cockpit", {0, 0, 270, (float)win.pointHeight}, "cockpit");
        if (gui.section("Cockpit", openCockpit)) {
            gui.columns(2);
            gui.toggle("Cockpit", showCockpit);
            gui.toggle("HUD", showHud);
            gui.columns(1);
            gui.menu("Size", resolution, {"320 x 200", "640 x 400", "1280 x 800"});
            gui.menu("Pixels", format, {"Indexed (palette)", "True colors"});
            gui.toggle("Smooth", cockpitOverlay.smooth);
            gui.menu("Palette", effect, {"VGA", "Fade", "Damage (red)", "Night (blue)", "Cycling"});
            if (effect == 1 || effect == 2 || effect == 3) {
                gui.slider("Amount", fadeAmount, 0, 1, "%.2f");
            }
        }
        if (gui.section("Post Process", openPost)) {
            ui::postProcessControls(gui, post.settings);
        }
        win.cameraControls(200);
        gui.endPanel();
        win.endGui();

        // the palette of the frame: the effects change only the 256 colors
        raster::Palette palette = vga;
        if (effect == 1) {
            palette = vga.fadedTo(raster::rgba(0, 0, 0), fadeAmount);
        } else if (effect == 2) {
            palette = vga.tinted(raster::rgba(255, 40, 30), fadeAmount);
        } else if (effect == 3) {
            palette = vga.tinted(raster::rgba(60, 90, 255), fadeAmount);
        } else if (effect == 4) {
            palette = vga.rotated(32, 55, (int)(time * 8)); // the hues turning
        }

        // the cockpit at the size and in the format asked
        int cw = 320 << resolution;
        int ch = 200 << resolution;
        raster::Format cf = format == 0 ? raster::Format::Indexed8 : raster::Format::RGBA8;
        if (cockpit.width() != cw || cockpit.format() != cf) {
            cockpit = Surface(cw, ch, cf);
        }
        drawCockpit(cockpit, palette, heading, pitch, roll, time, 320 + 40 * std::sin(time * 0.5f));
        cockpitOverlay.update(cockpit, &palette);
        // the HUD at the size of the window
        if (hud.width() != win.width || hud.height() != win.height) {
            hud = Surface(win.width, win.height, raster::Format::RGBA8);
        }
        if (showHud) {
            drawHud(hud, pitch, roll);
            hudOverlay.update(hud);
        }
        // the radar into a texture, on the screen in the scene
        drawRadar(radar, time);
        radarOverlay.update(radar, &palette);
        gpu::TextureHandle t = radarOverlay.texture();
        if (t.id != screenTexture.id) {
            assets::Material m;
            m.shading = assets::Shading::Unlit;
            m.doubleSided = true;
            m.diffuse = {1, 1, 1, 1};
            screen->clearDrawables();
            screen->addDrawable({screenGpu, renderer.createMaterial(m, t)});
            screenTexture = t;
        }

        // the scene, the cockpit and the HUD into the post process, then the panel
        gpu::RenderTargetHandle target = post.begin(win.width, win.height);
        renderer.setTarget(target);
        win.apply(camera, 0.5f, 1000);
        renderer.render(*root, camera, 0, 0, win.width, win.height);
        renderer.setTarget({});
        if (target) {
            gpu::ClearValues keep;
            keep.color = keep.depth = false;
            win.device->beginPass(target, 0, 0, win.width, win.height, keep);
        }
        // the cockpit in the part of the window beside the panel, at 4:3 (the
        // pixels of the VGA were not square)
        int left = (int)std::lround(270 * (win.pointWidth > 0 ? (float)win.width / win.pointWidth : 1));
        int ox;
        int oy;
        int ow;
        int oh;
        render::Overlay::fit(4.0f / 3.0f, win.width - left, win.height, ox, oy, ow, oh);
        ox += left;
        if (showHud) {
            hudOverlay.draw(0, 0, win.width, win.height, win.width, win.height);
        }
        if (showCockpit) {
            cockpitOverlay.draw(ox, oy, ow, oh, win.width, win.height);
        }
        if (target) {
            win.device->endPass();
        }
        post.end(0, 0, win.width, win.height, win.width, win.height);
        win.drawGui();
        win.swap();
    }
    return 0;
}
