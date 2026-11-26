// tests/audio_tests.cpp - r4nkine_audio on a loopback device of OpenAL Soft: the mix is rendered into memory, no sound card is needed.
#include "r4/audio/audio.h"
#include "r4/axes.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace r4;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static const int kRate = 48000;
// OpenAL Soft dithers its 16 bits output: silence is a noise of +-1
static const double kSilence = 4;

// a sine of 440 Hz, mono
static std::shared_ptr<audio::Sound> tone(float seconds) {
    std::vector<int16_t> pcm((size_t)(seconds * kRate));
    for (size_t i = 0; i < pcm.size(); i++) {
        pcm[i] = (int16_t)(12000 * std::sin(2 * 3.14159265 * 440 * (double)i / kRate));
    }
    return audio::Sound::fromPCM(pcm.data(), pcm.size(), 1, kRate);
}

// the energy of the left and right channels of frames rendered after an update
static void energy(audio::Context &ctx, int frames, double &left, double &right) {
    std::vector<int16_t> out((size_t)frames * 2);
    ctx.render(out.data(), frames);
    left = 0;
    right = 0;
    for (int i = 0; i < frames; i++) {
        left += (double)out[i * 2] * out[i * 2];
        right += (double)out[i * 2 + 1] * out[i * 2 + 1];
    }
}

// loop points: the introduction (silence) one time, then the body (a tone) again and again; the virtual time loops on the body
static void testLoopPoints(audio::Context &ctx) {
    const size_t part = kRate / 10;
    std::vector<int16_t> pcm(part * 2, 0);
    for (size_t i = part; i < pcm.size(); i++) {
        pcm[i] = (int16_t)(12000 * std::sin(2 * 3.14159265 * 440 * (double)i / kRate));
    }
    auto sound = audio::Sound::fromPCM(pcm.data(), pcm.size(), 1, kRate, part, part * 2);
    CHECK(sound && sound->loopStart() == part && sound->loopEnd() == part * 2);
    auto src = ctx.createSource(sound);
    src->loop = true;
    src->play();
    ctx.update(0.01f);
    double left = 0;
    double right = 0;
    energy(ctx, (int)part - 480, left, right);
    CHECK(left / (double)(part - 480) < kSilence);
    std::vector<int16_t> skip(480 * 2 * 2);
    ctx.render(skip.data(), 480 * 2);
    bool body = true;
    for (int i = 0; i < 5; i++) {
        energy(ctx, (int)part, left, right);
        body = body && left / (double)part > 1000;
    }
    CHECK(body);
    src->stop();
    ctx.update(0.01f);
    // virtual: after the introduction, its time stays in the body
    src->positional = true;
    src->position = {1000, 0, 0};
    src->maxDistance = 100;
    src->play();
    ctx.update(0.01f);
    CHECK(!src->hasVoice() && src->isPlaying());
    bool inBody = true;
    for (int i = 0; i < 20; i++) {
        ctx.update(0.037f);
        inBody = inBody && (i < 3 || (src->offset() >= 0.1f - 1e-4f && src->offset() < 0.2f + 1e-4f));
    }
    CHECK(inBody && src->isPlaying());
    src->stop();
    ctx.update(0.01f);
}

// a volume group: its gain multiplies the gain of its sources, and only of them
static void testGroups(audio::Context &ctx) {
    auto a = ctx.createSource(tone(1));
    a->loop = true;
    a->group = 2;
    a->play();
    ctx.update(0.01f);
    double left = 0;
    double right = 0;
    energy(ctx, 4800, left, right);
    const double full = left;
    ctx.setGroupGain(2, 0.5f);
    ctx.update(0.01f);
    energy(ctx, 2048, left, right);
    energy(ctx, 4800, left, right);
    CHECK(left > full * 0.2 && left < full * 0.3);
    ctx.setGroupGain(3, 0);
    ctx.update(0.01f);
    energy(ctx, 2048, left, right);
    energy(ctx, 4800, left, right);
    CHECK(left > full * 0.2 && left < full * 0.3);
    ctx.setGroupGain(2, 0);
    ctx.update(0.01f);
    energy(ctx, 2048, left, right);
    energy(ctx, 4800, left, right);
    CHECK(left / 4800 < kSilence);
    CHECK(ctx.groupGain(2) == 0 && ctx.groupGain(0) == 1 && ctx.groupGain(99) == 1);
    ctx.setGroupGain(2, 1);
    ctx.setGroupGain(3, 1);
    a->stop();
    ctx.update(0.01f);
}

// the listener at the origin, ahead +Y (the default of R4nkine): +X is on its right
static void testPan(audio::Context &ctx) {
    auto sound = tone(1);
    CHECK(sound && sound->channels() == 1 && sound->frames() == (size_t)kRate);
    auto src = ctx.createSource(sound);
    src->loop = true;
    src->positional = true;
    src->position = {10, 0, 0};
    src->play();
    ctx.update(0.016f);
    CHECK(src->hasVoice());
    double l = 0;
    double r = 0;
    energy(ctx, 4096, l, r);
    CHECK(r > 2 * l && r > 0);
    src->position = {-10, 0, 0};
    ctx.update(0.016f);
    energy(ctx, 4096, l, r);
    CHECK(l > 2 * r && l > 0);
    // a source without position plays at the listener: the same on both sides
    src->positional = false;
    ctx.update(0.016f);
    energy(ctx, 4096, l, r);
    CHECK(l > 0 && std::fabs(l - r) < 0.05 * (l + r));
    src->stop();
    ctx.update(0.016f);
    CHECK(!src->hasVoice());
}

// the listener follows a node turned to face -X: a source at -X is ahead, one at +Y is on its right
static void testListenerNode(audio::Context &ctx) {
    auto root = scene::make<scene::Group>();
    auto head = scene::make<scene::Group>();
    head->setMatrix(rotate(radians(90.0f), axes::kUp));
    root->addChild(head);
    ctx.listener().follow(head);
    auto src = ctx.createSource(tone(1));
    src->loop = true;
    src->positional = true;
    src->position = {0, 10, 0};
    src->play();
    ctx.update(*root, 0.016f);
    CHECK(length(ctx.listener().forward() - Vec3{-1, 0, 0}) < 1e-4f);
    double l = 0;
    double r = 0;
    energy(ctx, 4096, l, r);
    CHECK(r > 2 * l);
    ctx.listener().follow(nullptr);
    ctx.listener().fromView(lookAt({0, 0, 0}, {0, 1, 0}, axes::kUp));
    ctx.update(0.016f);
    CHECK(length(ctx.listener().forward() - Vec3{0, 1, 0}) < 1e-4f);
    energy(ctx, 4096, l, r);
    // a camera looking at +Y: +Y is ahead, the two sides alike
    CHECK(std::fabs(l - r) < 0.2 * (l + r));
    src->stop();
    ctx.update(0.016f);
}

// onFinished after the end of a sound, on the thread of the update; also when the source has no voice
static void testFinished(audio::Context &ctx) {
    int finished = 0;
    auto src = ctx.createSource(tone(0.1f));
    src->onFinished = [&](audio::Source &) {
        finished++;
    };
    src->play();
    for (int i = 0; i < 20; i++) {
        ctx.update(1.0f / 60);
        std::vector<int16_t> out(800 * 2);
        ctx.render(out.data(), 800);
    }
    CHECK(finished == 1);
    CHECK(!src->isPlaying() && !src->hasVoice());
    // out of reach: virtual, its time goes on, it ends all the same
    src->positional = true;
    src->position = {1000, 0, 0};
    src->maxDistance = 100;
    src->play();
    ctx.update(0.05f);
    CHECK(!src->hasVoice() && src->isPlaying());
    ctx.update(0.05f);
    ctx.update(0.05f);
    CHECK(finished == 2 && !src->isPlaying());
    // stop gives onFinished at the next update
    src->positional = false;
    src->play();
    ctx.update(0.01f);
    src->stop();
    ctx.update(0.01f);
    CHECK(finished == 3);
}

// one voice: the nearer source has it; the priority comes first; a masked branch has none
static void testVoices() {
    audio::Settings settings;
    settings.maxVoices = 1;
    std::string err;
    auto ctx = audio::Context::createLoopback(kRate, settings, &err);
    CHECK(ctx && ctx->voices() == 1);
    if (!ctx) {
        return;
    }
    auto sound = tone(1);
    auto root = scene::make<scene::Group>();
    auto nearBranch = scene::make<scene::Group>();
    auto a = std::make_shared<audio::SoundSource>(ctx->createSource(sound));
    auto b = std::make_shared<audio::SoundSource>(ctx->createSource(sound));
    a->setMatrix(translate({0, 10, 0}));
    b->setMatrix(translate({0, 50, 0}));
    nearBranch->addChild(a);
    root->addChild(nearBranch);
    root->addChild(b);
    for (auto *s : {&a->source(), &b->source()}) {
        s->loop = true;
        s->play();
    }
    ctx->update(*root, 0.016f);
    CHECK(a->source().hasVoice() && !b->source().hasVoice());
    a->setMatrix(translate({0, 80, 0}));
    ctx->update(*root, 0.016f);
    CHECK(!a->source().hasVoice() && b->source().hasVoice());
    b->source().priority = -1;
    ctx->update(*root, 0.016f);
    CHECK(a->source().hasVoice() && !b->source().hasVoice());
    // the moves give the velocity: 80 -> 90 in 0.5 s
    a->setMatrix(translate({0, 90, 0}));
    ctx->update(*root, 0.5f);
    CHECK(length(a->source().velocity - Vec3{0, 20, 0}) < 1e-3f);
    // the branch out of the mask: silenced, the other one gets the voice
    nearBranch->setAppMask(2);
    ctx->update(*root, 0.016f, 1);
    CHECK(!a->source().hasVoice() && b->source().hasVoice() && a->source().isPlaying());
    a->source().stop();
    b->source().stop();
    ctx->update(*root, 0.016f, 1);
    CHECK(ctx->voicesInUse() == 0);
}

// a stream: fill makes a square wave; the rendered output carries it
static void testStream(audio::Context &ctx) {
    int calls = 0;
    auto stream = ctx.createStream(
        2, kRate,
        [&](int16_t *out, int frames) {
            calls++;
            for (int i = 0; i < frames * 2; i++) {
                out[i] = (i / 2) % 100 < 50 ? 8000 : -8000;
            }
        },
        50);
    CHECK(stream && stream->channels() == 2);
    if (!stream) {
        return;
    }
    double l = 0;
    double r = 0;
    // the fade out of the sources stopped before (OpenAL Soft avoids clicks), then silence
    energy(ctx, 4096, l, r);
    energy(ctx, 1024, l, r);
    CHECK(calls == 0);
    CHECK(l < 1024 * kSilence);
    stream->source().play();
    ctx.update(0.016f);
    energy(ctx, 4096, l, r);
    CHECK(calls >= 4 && l > 4096 * 1000 && r > 4096 * 1000);
    uint64_t before = stream->framesFilled();
    for (int i = 0; i < 10; i++) {
        energy(ctx, 4096, l, r);
    }
    CHECK(stream->framesFilled() > before);
    stream->source().pause();
    energy(ctx, 4096, l, r);
    energy(ctx, 1024, l, r);
    CHECK(l < 1024 * kSilence);
    // play again after pause: it goes on
    stream->source().play();
    energy(ctx, 4096, l, r);
    CHECK(l > 4096 * 1000);
    stream->source().stop();
}

// the zero crossings of the left channel: twice the frequency
static int crossings(audio::Context &ctx, int frames) {
    std::vector<int16_t> out((size_t)frames * 2);
    ctx.render(out.data(), frames);
    // with a margin, so that the dither about 0 counts for nothing
    int n = 0;
    int side = 0;
    for (int i = 0; i < frames; i++) {
        int v = out[i * 2];
        int now = v > 8 ? 1 : (v < -8 ? -1 : side);
        n += side != 0 && now != side ? 1 : 0;
        side = now;
    }
    return n;
}

// a node that comes to the listener at a third of the speed of sound: higher by 1.5 (343 / (343 - 114.3))
static void testDoppler(audio::Context &ctx) {
    ctx.setSpeedOfSound(343);
    auto root = scene::make<scene::Group>();
    auto node = std::make_shared<audio::SoundSource>(ctx.createSource(tone(1)));
    root->addChild(node);
    node->source().loop = true;
    node->source().play();
    node->setMatrix(translate({0, 200, 0}));
    ctx.update(*root, 0.1f);
    ctx.update(*root, 0.1f);
    int still = crossings(ctx, 8192);
    node->setMatrix(translate({0, 200 - 11.43f, 0}));
    ctx.update(*root, 0.1f);
    CHECK(length(node->source().velocity - Vec3{0, -114.3f, 0}) < 0.1f);
    crossings(ctx, 2048);
    int coming = crossings(ctx, 8192);
    CHECK(coming > still * 1.4 && coming < still * 1.6);
    ctx.setDoppler(0);
    ctx.update(*root, 0.1f);
    crossings(ctx, 2048);
    int off = crossings(ctx, 8192);
    CHECK(std::abs(off - still) <= still / 20);
    ctx.setDoppler(1);
    node->source().stop();
    ctx.update(*root, 0.1f);
}

// a context on a device (the null driver of OpenAL Soft): its audio thread fills the stream without render
static void testThread() {
    std::string err;
    auto ctx = audio::Context::create({}, &err);
    if (!ctx) {
        std::printf("no device, the audio thread is not tested: %s\n", err.c_str());
        return;
    }
    CHECK(!ctx->loopback());
    int calls = 0;
    auto stream = ctx->createStream(
        1, kRate,
        [&](int16_t *out, int frames) {
            calls++;
            for (int i = 0; i < frames; i++) {
                out[i] = 0;
            }
        },
        40);
    stream->source().play();
    for (int i = 0; i < 30; i++) {
        ctx->update(0.01f);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(calls > 4 && stream->framesFilled() > (uint64_t)kRate / 20);
    stream->source().stop();
}

// a WAV file of 8 bits, stereo
static void testWAV(const std::string &dir) {
    std::string path = dir + "/r4_audio_test.wav";
    FILE *f = std::fopen(path.c_str(), "wb");
    CHECK(f != nullptr);
    if (!f) {
        return;
    }
    const int frames = 100;
    const int channels = 2;
    const int rate = 22050;
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) {
            std::fputc((int)(v >> (8 * i) & 0xff), f);
        }
    };
    auto put16 = [&](uint16_t v) {
        std::fputc(v & 0xff, f);
        std::fputc(v >> 8, f);
    };
    std::fwrite("RIFF", 1, 4, f);
    put32(36 + frames * channels);
    std::fwrite("WAVEfmt ", 1, 8, f);
    put32(16);
    put16(1);
    put16(channels);
    put32(rate);
    put32(rate * channels);
    put16(channels);
    put16(8);
    std::fwrite("data", 1, 4, f);
    put32(frames * channels);
    for (int i = 0; i < frames * channels; i++) {
        std::fputc(128 + (i % 2 ? 50 : -50), f);
    }
    std::fclose(f);
    std::string err;
    auto s = audio::loadWAV(path, &err);
    CHECK(s && s->frames() == (size_t)frames && s->channels() == channels && s->rate() == rate);
    CHECK(!audio::loadWAV(dir + "/missing.wav", &err) && !err.empty());
    std::remove(path.c_str());
}

int main(int argc, char **argv) {
    std::string dir = argc > 1 ? argv[1] : ".";
    // the device of testThread: the null driver, so that the tests need no sound card
#ifdef _WIN32
    _putenv("ALSOFT_DRIVERS=null");
#else
    setenv("ALSOFT_DRIVERS", "null", 1);
#endif
    std::string err;
    auto ctx = audio::Context::createLoopback(kRate, {}, &err);
    CHECK(ctx != nullptr);
    if (!ctx) {
        std::printf("no loopback context: %s\n", err.c_str());
        std::printf("%d checks, %d failures\n", checks, failures);
        return 1;
    }
    CHECK(ctx->loopback() && ctx->voices() > 1);
    testPan(*ctx);
    testListenerNode(*ctx);
    testFinished(*ctx);
    testLoopPoints(*ctx);
    testGroups(*ctx);
    testStream(*ctx);
    testDoppler(*ctx);
    testWAV(dir);
    ctx.reset();
    testVoices();
    testThread();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
