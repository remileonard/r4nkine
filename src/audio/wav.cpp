// src/audio/wav.cpp - the WAV files of PCM, 8 or 16 bits, mono or stereo, as Sounds of r4nkine_audio.
#include "r4/audio/audio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace r4::audio {

namespace {

uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint16_t u16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

} // namespace

std::shared_ptr<Sound> loadWAV(const std::string &path, std::string *error) {
    auto fail = [&](const char *why) -> std::shared_ptr<Sound> {
        if (error) {
            *error = path + ": " + why;
        }
        return nullptr;
    };
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return fail("cannot open");
    }
    std::vector<uint8_t> d;
    uint8_t chunk[65536];
    size_t n = 0;
    while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) {
        d.insert(d.end(), chunk, chunk + n);
    }
    std::fclose(f);
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        return fail("not a WAV file");
    }
    int channels = 0;
    int rate = 0;
    int bits = 0;
    int format = 0;
    const uint8_t *data = nullptr;
    size_t dataSize = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const uint8_t *c = d.data() + pos;
        size_t size = u32(c + 4);
        size_t body = pos + 8;
        size_t available = std::min(size, d.size() - body);
        if (std::memcmp(c, "fmt ", 4) == 0 && available >= 16) {
            format = u16(c + 8);
            channels = u16(c + 10);
            rate = (int)u32(c + 12);
            bits = u16(c + 22);
        } else if (std::memcmp(c, "data", 4) == 0) {
            data = d.data() + body;
            dataSize = available;
        }
        pos = body + size + (size & 1);
    }
    if (format != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || !data) {
        return fail("only PCM of 8 or 16 bits, mono or stereo");
    }
    const size_t bytesPerFrame = (size_t)channels * (size_t)(bits / 8);
    const size_t frames = dataSize / bytesPerFrame;
    std::vector<int16_t> pcm(frames * channels);
    for (size_t i = 0; i < pcm.size(); i++) {
        if (bits == 8) {
            pcm[i] = (int16_t)((int)data[i] - 128) * 256;
        } else {
            pcm[i] = (int16_t)u16(data + i * 2);
        }
    }
    std::shared_ptr<Sound> s = Sound::fromPCM(pcm.data(), frames, channels, rate);
    if (!s) {
        return fail("no audio context, or OpenAL refused the data");
    }
    return s;
}

} // namespace r4::audio
