// r4/audio/audio.h - the audio of R4nkine with OpenAL Soft (r4nkine_audio): sounds, sources, streams, a listener, and sound sources in the graph updated after APP.
// The positions are in the world of R4nkine (Z up); the module turns them to the axes of OpenAL. Refer to docs/AUDIO.md.
#pragma once

#include "r4/math.h"
#include "r4/scene/node.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace r4::audio {

class Context;
class Stream;

// a PCM buffer of 16 bits in OpenAL; mono for a sound in space. Made with a context, freed before it.
class Sound {
public:
    ~Sound();
    Sound(const Sound &) = delete;
    Sound &operator=(const Sound &) = delete;

    // frames of channels samples (1 or 2), interleaved; null without a current context. With loopEnd > loopStart, a source with
    // loop plays the frames before loopStart one time, then loops on [loopStart, loopEnd) (AL_SOFT_loop_points)
    static std::shared_ptr<Sound> fromPCM(const int16_t *samples, size_t frames, int channels, int rate, size_t loopStart = 0, size_t loopEnd = 0);

    int channels() const {
        return channels_;
    }
    int rate() const {
        return rate_;
    }
    size_t frames() const {
        return frames_;
    }
    float seconds() const {
        return rate_ > 0 ? (float)frames_ / (float)rate_ : 0.0f;
    }
    uint32_t buffer() const {
        return buffer_;
    }
    // the loop in frames; [0, frames) without loop points
    size_t loopStart() const {
        return loopStart_;
    }
    size_t loopEnd() const {
        return loopEnd_;
    }

private:
    Sound() = default;
    uint32_t buffer_ = 0;
    size_t loopStart_ = 0;
    size_t loopEnd_ = 0;
    int channels_ = 0;
    int rate_ = 0;
    size_t frames_ = 0;
};

// PCM of 8 or 16 bits, mono or stereo
std::shared_ptr<Sound> loadWAV(const std::string &path, std::string *error = nullptr);

enum class DistanceModel {
    NONE,
    INVERSE,
    INVERSE_CLAMPED,
    LINEAR,
    LINEAR_CLAMPED,
    EXPONENT,
    EXPONENT_CLAMPED
};

// A sound that plays. Its parameters go to OpenAL at each update. Without a voice (too far, too many sources, its branch out of
// the mask), it is virtual: its time goes on, and it continues from there when it gets a voice again.
class Source {
public:
    ~Source();
    Source(const Source &) = delete;
    Source &operator=(const Source &) = delete;

    void setSound(std::shared_ptr<Sound> sound);
    const std::shared_ptr<Sound> &sound() const {
        return sound_;
    }
    void play();
    void pause();
    // onFinished at the next update
    void stop();
    bool isPlaying() const;
    bool isPaused() const;
    // seconds from the start of the sound
    float offset() const;
    void setOffset(float seconds);
    bool hasVoice() const {
        return voice_ >= 0;
    }

    float gain = 1;
    float pitch = 1;
    bool loop = false;
    // false: at the listener (a voice, the radio, a menu); true: at position, in the world
    bool positional = false;
    Vec3 position;
    Vec3 velocity;
    // the axis of the cone
    Vec3 direction{0, 1, 0};
    float referenceDistance = 1;
    // beyond it the source has no voice
    float maxDistance = 1e30f;
    float rolloff = 1;
    // degrees; 360: no cone
    float innerAngle = 360;
    float outerAngle = 360;
    float outerGain = 0;
    // the higher first when there are not enough voices
    int priority = 0;
    // at the update after the end of a sound without loop, or after stop(); on the thread of the application
    std::function<void(Source &)> onFinished;

private:
    friend class Context;
    friend class Stream;
    enum class State {
        STOPPED,
        PLAYING,
        PAUSED
    };
    explicit Source(Context &context);
    Context &context_;
    std::shared_ptr<Sound> sound_;
    State state_ = State::STOPPED;
    // the time when it has no voice, in seconds
    double time_ = 0;
    int voice_ = -1;
    bool finished_ = false;
    bool restart_ = false;
    Stream *stream_ = nullptr;
    // set by update for the sources of the graph
    bool inGraph_ = false;
    bool graphSeen_ = false;
    bool silenced_ = false;
    float distance_ = 0;
};

// A sound made while it plays: fill writes the next frames (16 bits, interleaved). The audio thread of the context calls it to keep a
// queue of buffers full, as Mix_HookMusic of SDL_mixer: protect what it reads with a lock.
class Stream {
public:
    using Fill = std::function<void(int16_t *out, int frames)>;
    ~Stream();
    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;

    // its gain, its position, play, pause and stop; it always has its own voice
    Source &source() {
        return *source_;
    }
    int channels() const {
        return channels_;
    }
    int rate() const {
        return rate_;
    }
    // the frames filled since its creation
    uint64_t framesFilled() const {
        return filled_;
    }

private:
    friend class Context;
    friend class Source;
    Stream(Context &context, int channels, int rate, Fill fill, int latencyMs);
    void service();
    Context &context_;
    std::unique_ptr<Source> source_;
    int channels_ = 0;
    int rate_ = 0;
    Fill fill_;
    int bufferFrames_ = 0;
    std::vector<uint32_t> buffers_;
    std::vector<int16_t> scratch_;
    uint32_t alSource_ = 0;
    uint64_t filled_ = 0;
    bool started_ = false;
};

// Where the sounds are heard. Its velocity comes from its moves between two updates.
class Listener {
public:
    // a position, the axis ahead and the axis up, in the world
    void setPose(Vec3 position, Vec3 forward, Vec3 up);
    // a camera of the renderer (its view matrix: -Z ahead, +Y up)
    void fromView(const Mat4 &view);
    // the world from a camera (-Z ahead, +Y up): the head of xr::Session::head()
    void fromCameraPose(const Mat4 &worldFromCamera);
    // a node of the world (+Y ahead, +Z up, as R4nkine): attached until follow(nullptr); update takes its world matrix
    void follow(const scene::NodePtr &node);
    Vec3 position() const {
        return position_;
    }
    Vec3 forward() const {
        return forward_;
    }
    Vec3 up() const {
        return up_;
    }
    Vec3 velocity() const {
        return velocity_;
    }
    float gain = 1;

private:
    friend class Context;
    void set(Vec3 position, Vec3 forward, Vec3 up);
    Vec3 position_;
    Vec3 forward_{0, 1, 0};
    Vec3 up_{0, 0, 1};
    Vec3 velocity_;
    Vec3 last_;
    bool hasLast_ = false;
    std::weak_ptr<scene::Node> follow_;
    bool following_ = false;
    bool hasPose_ = false;
};

struct Settings {
    // the name of the OpenAL device; empty: the default one
    std::string device;
    // the HRTF of OpenAL Soft, for headphones
    bool hrtf = false;
    // the sources heard at once; the others are virtual. The streams have their own voices.
    int maxVoices = 64;
};

// The OpenAL device and context, the voices and the audio thread. One for each program; made current when it is made.
class Context {
public:
    static std::unique_ptr<Context> create(const Settings &settings = {}, std::string *error = nullptr);
    // without a device: stereo 16 bits rendered into memory by render (tests, recording); the streams are filled by render
    static std::unique_ptr<Context> createLoopback(int rate, const Settings &settings = {}, std::string *error = nullptr);
    ~Context();
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    std::shared_ptr<Source> createSource(std::shared_ptr<Sound> sound = nullptr);
    std::shared_ptr<Stream> createStream(int channels, int rate, Stream::Fill fill, int latencyMs = 100);

    Listener &listener() {
        return listener_;
    }
    // in the units of the world: 343 for meters, 1125 for feet
    void setSpeedOfSound(float speed);
    // 0: no Doppler effect
    void setDoppler(float factor);
    void setDistanceModel(DistanceModel model);

    // the sources, the listener and the voices: once a frame, after scene::app and before the draw
    void update(float dt);
    // the same, with the sound sources of the graph below root whose app mask has a bit of mask
    void update(scene::Node &root, float dt, uint32_t mask = 0xffffffffu);
    // loopback only
    void render(int16_t *out, int frames);

    bool loopback() const {
        return loopback_;
    }
    bool hrtf() const {
        return hrtf_;
    }
    const std::string &deviceName() const {
        return deviceName_;
    }
    int voices() const {
        return (int)voices_.size();
    }
    int voicesInUse() const;

private:
    friend class Source;
    friend class Stream;
    struct Impl;
    Context();
    bool open(const Settings &settings, int loopbackRate, std::string *error);
    void assignVoices();
    void bindVoice(Source &s, int voice);
    void releaseVoice(Source &s, bool keepTime);
    void apply(Source &s, uint32_t alSource);
    void serviceStreams();
    void forget(Source *s);
    void forget(Stream *s);
    void visit(scene::Node &node, const Mat4 &parent, float dt, uint32_t mask);

    std::unique_ptr<Impl> impl_;
    Listener listener_;
    std::vector<uint32_t> voices_;
    std::vector<Source *> voiceOwner_;
    std::vector<std::weak_ptr<Source>> sources_;
    std::vector<Stream *> streams_;
    std::vector<Source *> finished_;
    bool loopback_ = false;
    bool hrtf_ = false;
    std::string deviceName_;
};

// A node that plays its source where it is: its world matrix gives the position and the direction (+Y), its moves give the velocity.
class SoundSource : public scene::Node {
public:
    explicit SoundSource(std::shared_ptr<Source> source) : source_(std::move(source)) {
    }
    Source &source() {
        return *source_;
    }
    const std::shared_ptr<Source> &sourcePtr() const {
        return source_;
    }

private:
    friend class Context;
    std::shared_ptr<Source> source_;
    Vec3 last_;
    bool hasLast_ = false;
};

} // namespace r4::audio
