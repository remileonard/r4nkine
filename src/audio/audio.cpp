// src/audio/audio.cpp - r4nkine_audio on OpenAL Soft: the context, its voices and its audio thread, the sources, the streams, the listener.
#include "r4/audio/audio.h"
#include "r4/axes.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace r4::audio {

namespace {

// the world of R4nkine (Z up) in the axes of OpenAL (Y up)
void alVector(ALenum param, Vec3 v, uint32_t source, bool listener) {
    Vec3 a = axes::toYUp(v);
    if (listener) {
        alListener3f(param, a.x, a.y, a.z);
    } else {
        alSource3f(source, param, a.x, a.y, a.z);
    }
}

ALenum alDistanceModelOf(DistanceModel m) {
    switch (m) {
    case DistanceModel::NONE:
        return AL_NONE;
    case DistanceModel::INVERSE:
        return AL_INVERSE_DISTANCE;
    case DistanceModel::LINEAR:
        return AL_LINEAR_DISTANCE;
    case DistanceModel::LINEAR_CLAMPED:
        return AL_LINEAR_DISTANCE_CLAMPED;
    case DistanceModel::EXPONENT:
        return AL_EXPONENT_DISTANCE;
    case DistanceModel::EXPONENT_CLAMPED:
        return AL_EXPONENT_DISTANCE_CLAMPED;
    default:
        return AL_INVERSE_DISTANCE_CLAMPED;
    }
}

ALenum formatOf(int channels) {
    return channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
}

} // namespace

struct Context::Impl {
    ALCdevice *device = nullptr;
    ALCcontext *context = nullptr;
    LPALCRENDERSAMPLESSOFT renderSamples = nullptr;
    std::recursive_mutex mutex;
    std::thread thread;
    std::atomic<bool> running{false};
};

// ---- Sound ----

Sound::~Sound() {
    if (buffer_) {
        ALuint b = buffer_;
        alDeleteBuffers(1, &b);
    }
}

std::shared_ptr<Sound> Sound::fromPCM(const int16_t *samples, size_t frames, int channels, int rate) {
    if (!samples || frames == 0 || (channels != 1 && channels != 2) || rate <= 0 || !alcGetCurrentContext()) {
        return nullptr;
    }
    std::shared_ptr<Sound> s(new Sound());
    ALuint b = 0;
    alGenBuffers(1, &b);
    alBufferData(b, formatOf(channels), samples, (ALsizei)(frames * channels * sizeof(int16_t)), rate);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &b);
        return nullptr;
    }
    s->buffer_ = b;
    s->channels_ = channels;
    s->rate_ = rate;
    s->frames_ = frames;
    return s;
}

// ---- Source ----

Source::Source(Context &context) : context_(context) {
}

Source::~Source() {
    context_.forget(this);
}

void Source::setSound(std::shared_ptr<Sound> sound) {
    std::lock_guard<std::recursive_mutex> lock(context_.impl_->mutex);
    if (voice_ >= 0) {
        context_.releaseVoice(*this, false);
    }
    sound_ = std::move(sound);
    time_ = 0;
}

void Source::play() {
    std::lock_guard<std::recursive_mutex> lock(context_.impl_->mutex);
    if (state_ == State::PLAYING || state_ == State::STOPPED) {
        time_ = 0;
        if (voice_ >= 0) {
            ALuint al = context_.voices_[voice_];
            alSourceStop(al);
            alSourcef(al, AL_SEC_OFFSET, 0);
            alSourcePlay(al);
        }
    }
    if (state_ == State::PAUSED && stream_ && stream_->started_) {
        alSourcePlay(stream_->alSource_);
    }
    state_ = State::PLAYING;
    finished_ = false;
    restart_ = true;
}

void Source::pause() {
    std::lock_guard<std::recursive_mutex> lock(context_.impl_->mutex);
    if (state_ != State::PLAYING) {
        return;
    }
    state_ = State::PAUSED;
    if (stream_) {
        alSourcePause(stream_->alSource_);
    } else if (voice_ >= 0) {
        context_.releaseVoice(*this, true);
    }
}

void Source::stop() {
    std::lock_guard<std::recursive_mutex> lock(context_.impl_->mutex);
    if (state_ == State::STOPPED) {
        return;
    }
    state_ = State::STOPPED;
    time_ = 0;
    finished_ = true;
    if (stream_) {
        ALuint al = stream_->alSource_;
        alSourceStop(al);
        alSourcei(al, AL_BUFFER, 0);
        stream_->started_ = false;
    } else if (voice_ >= 0) {
        context_.releaseVoice(*this, false);
    }
}

bool Source::isPlaying() const {
    return state_ == State::PLAYING;
}

bool Source::isPaused() const {
    return state_ == State::PAUSED;
}

float Source::offset() const {
    if (voice_ >= 0) {
        ALfloat seconds = 0;
        alGetSourcef(context_.voices_[voice_], AL_SEC_OFFSET, &seconds);
        return seconds;
    }
    return (float)time_;
}

void Source::setOffset(float seconds) {
    std::lock_guard<std::recursive_mutex> lock(context_.impl_->mutex);
    time_ = std::max(0.0f, seconds);
    if (voice_ >= 0) {
        alSourcef(context_.voices_[voice_], AL_SEC_OFFSET, (ALfloat)time_);
    }
}

// ---- Stream ----

Stream::Stream(Context &context, int channels, int rate, Fill fill, int latencyMs) : context_(context), channels_(channels), rate_(rate), fill_(std::move(fill)) {
    source_.reset(new Source(context));
    source_->stream_ = this;
    const int kBuffers = 4;
    bufferFrames_ = std::max(256, rate * std::max(20, latencyMs) / 1000 / kBuffers);
    buffers_.resize(kBuffers);
    alGenBuffers(kBuffers, buffers_.data());
    ALuint al = 0;
    alGenSources(1, &al);
    alSource_ = al;
    scratch_.resize((size_t)bufferFrames_ * channels_);
}

Stream::~Stream() {
    context_.forget(this);
    ALuint al = alSource_;
    alSourceStop(al);
    alSourcei(al, AL_BUFFER, 0);
    alDeleteSources(1, &al);
    alDeleteBuffers((ALsizei)buffers_.size(), buffers_.data());
}

// on the audio thread (or in render), under the lock of the context
void Stream::service() {
    if (source_->state_ != Source::State::PLAYING) {
        return;
    }
    auto queue = [&](ALuint b) {
        std::fill(scratch_.begin(), scratch_.end(), 0);
        if (fill_) {
            fill_(scratch_.data(), bufferFrames_);
        }
        alBufferData(b, formatOf(channels_), scratch_.data(), (ALsizei)(scratch_.size() * sizeof(int16_t)), rate_);
        alSourceQueueBuffers(alSource_, 1, &b);
        filled_ += (uint64_t)bufferFrames_;
    };
    if (!started_) {
        alSourceStop(alSource_);
        alSourcei(alSource_, AL_BUFFER, 0);
        for (ALuint b : buffers_) {
            queue(b);
        }
        alSourcePlay(alSource_);
        started_ = true;
        return;
    }
    ALint processed = 0;
    alGetSourcei(alSource_, AL_BUFFERS_PROCESSED, &processed);
    for (ALint i = 0; i < processed; i++) {
        ALuint b = 0;
        alSourceUnqueueBuffers(alSource_, 1, &b);
        queue(b);
    }
    ALint state = 0;
    alGetSourcei(alSource_, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        // an underrun: the queue ran dry
        alSourcePlay(alSource_);
    }
}

// ---- Listener ----

void Listener::set(Vec3 position, Vec3 forward, Vec3 up) {
    position_ = position;
    forward_ = normalize(forward);
    up_ = normalize(up);
}

void Listener::fromCameraPose(const Mat4 &worldFromCamera) {
    follow_.reset();
    following_ = false;
    set(Vec3(worldFromCamera[3]), transformVector(worldFromCamera, {0, 0, -1}), transformVector(worldFromCamera, {0, 1, 0}));
}

void Listener::fromView(const Mat4 &view) {
    fromCameraPose(inverse(view));
}

void Listener::follow(const scene::NodePtr &node) {
    follow_ = node;
    following_ = node != nullptr;
}

// ---- Context ----

Context::Context() : impl_(std::make_unique<Impl>()) {
}

std::unique_ptr<Context> Context::create(const Settings &settings, std::string *error) {
    std::unique_ptr<Context> c(new Context());
    if (!c->open(settings, 0, error)) {
        return nullptr;
    }
    return c;
}

std::unique_ptr<Context> Context::createLoopback(int rate, const Settings &settings, std::string *error) {
    std::unique_ptr<Context> c(new Context());
    if (!c->open(settings, std::max(8000, rate), error)) {
        return nullptr;
    }
    return c;
}

bool Context::open(const Settings &settings, int loopbackRate, std::string *error) {
    auto fail = [&](const std::string &why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    loopback_ = loopbackRate > 0;
    std::vector<ALCint> attrs;
    if (loopback_) {
        auto open = (LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT");
        impl_->renderSamples = (LPALCRENDERSAMPLESSOFT)alcGetProcAddress(nullptr, "alcRenderSamplesSOFT");
        if (!open || !impl_->renderSamples) {
            return fail("OpenAL: no ALC_SOFT_loopback");
        }
        impl_->device = open(nullptr);
        attrs = {ALC_FREQUENCY, loopbackRate, ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT};
    } else {
        impl_->device = alcOpenDevice(settings.device.empty() ? nullptr : settings.device.c_str());
    }
    if (!impl_->device) {
        return fail("OpenAL: no device " + settings.device);
    }
    attrs.push_back(ALC_HRTF_SOFT);
    attrs.push_back(settings.hrtf ? ALC_TRUE : ALC_FALSE);
    attrs.push_back(0);
    impl_->context = alcCreateContext(impl_->device, attrs.data());
    if (!impl_->context || !alcMakeContextCurrent(impl_->context)) {
        return fail("OpenAL: no context on " + settings.device);
    }
    const ALCchar *name = alcGetString(impl_->device, ALC_ALL_DEVICES_SPECIFIER);
    deviceName_ = name ? name : "";
    ALCint hrtf = 0;
    alcGetIntegerv(impl_->device, ALC_HRTF_SOFT, 1, &hrtf);
    hrtf_ = hrtf == ALC_TRUE;
    alGetError();
    for (int i = 0; i < std::max(1, settings.maxVoices); i++) {
        ALuint al = 0;
        alGenSources(1, &al);
        if (alGetError() != AL_NO_ERROR) {
            break;
        }
        voices_.push_back(al);
    }
    voiceOwner_.assign(voices_.size(), nullptr);
    if (!loopback_) {
        impl_->running = true;
        impl_->thread = std::thread([this]() {
            while (impl_->running) {
                serviceStreams();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }
    return true;
}

Context::~Context() {
    impl_->running = false;
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    if (!voices_.empty()) {
        for (ALuint al : voices_) {
            alSourceStop(al);
        }
        alDeleteSources((ALsizei)voices_.size(), voices_.data());
    }
    alcMakeContextCurrent(nullptr);
    if (impl_->context) {
        alcDestroyContext(impl_->context);
    }
    if (impl_->device) {
        alcCloseDevice(impl_->device);
    }
}

std::shared_ptr<Source> Context::createSource(std::shared_ptr<Sound> sound) {
    std::shared_ptr<Source> s(new Source(*this));
    s->sound_ = std::move(sound);
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    sources_.push_back(s);
    return s;
}

std::shared_ptr<Stream> Context::createStream(int channels, int rate, Stream::Fill fill, int latencyMs) {
    if ((channels != 1 && channels != 2) || rate <= 0) {
        return nullptr;
    }
    std::shared_ptr<Stream> s(new Stream(*this, channels, rate, std::move(fill), latencyMs));
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    streams_.push_back(s.get());
    return s;
}

void Context::forget(Source *s) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    if (s->voice_ >= 0) {
        releaseVoice(*s, false);
    }
    finished_.erase(std::remove(finished_.begin(), finished_.end(), s), finished_.end());
}

void Context::forget(Stream *s) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    streams_.erase(std::remove(streams_.begin(), streams_.end(), s), streams_.end());
}

void Context::setSpeedOfSound(float speed) {
    alSpeedOfSound(std::max(1.0f, speed));
}

void Context::setDoppler(float factor) {
    alDopplerFactor(std::max(0.0f, factor));
}

void Context::setDistanceModel(DistanceModel model) {
    alDistanceModel(alDistanceModelOf(model));
}

int Context::voicesInUse() const {
    int n = 0;
    for (const Source *s : voiceOwner_) {
        n += s ? 1 : 0;
    }
    return n;
}

void Context::serviceStreams() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    for (Stream *s : streams_) {
        s->service();
    }
}

void Context::render(int16_t *out, int frames) {
    if (!loopback_ || !impl_->renderSamples) {
        return;
    }
    serviceStreams();
    impl_->renderSamples(impl_->device, out, frames);
}

void Context::apply(Source &s, uint32_t al) {
    alSourcef(al, AL_GAIN, std::max(0.0f, s.gain));
    alSourcef(al, AL_PITCH, std::clamp(s.pitch, 0.01f, 16.0f));
    if (!s.stream_) {
        alSourcei(al, AL_LOOPING, s.loop ? AL_TRUE : AL_FALSE);
    }
    alSourcei(al, AL_SOURCE_RELATIVE, s.positional ? AL_FALSE : AL_TRUE);
    alVector(AL_POSITION, s.positional ? s.position : Vec3(0.0f), al, false);
    alVector(AL_VELOCITY, s.positional ? s.velocity : Vec3(0.0f), al, false);
    alVector(AL_DIRECTION, s.innerAngle < 360 ? s.direction : Vec3(0.0f), al, false);
    alSourcef(al, AL_REFERENCE_DISTANCE, std::max(0.0f, s.referenceDistance));
    alSourcef(al, AL_MAX_DISTANCE, std::max(0.0f, s.maxDistance));
    alSourcef(al, AL_ROLLOFF_FACTOR, std::max(0.0f, s.rolloff));
    alSourcef(al, AL_CONE_INNER_ANGLE, s.innerAngle);
    alSourcef(al, AL_CONE_OUTER_ANGLE, s.outerAngle);
    alSourcef(al, AL_CONE_OUTER_GAIN, s.outerGain);
}

void Context::bindVoice(Source &s, int voice) {
    ALuint al = voices_[voice];
    voiceOwner_[voice] = &s;
    s.voice_ = voice;
    alSourceStop(al);
    alSourcei(al, AL_BUFFER, (ALint)s.sound_->buffer());
    apply(s, al);
    alSourcef(al, AL_SEC_OFFSET, (ALfloat)s.time_);
    alSourcePlay(al);
}

void Context::releaseVoice(Source &s, bool keepTime) {
    if (s.voice_ < 0) {
        return;
    }
    ALuint al = voices_[s.voice_];
    if (keepTime) {
        ALfloat seconds = 0;
        alGetSourcef(al, AL_SEC_OFFSET, &seconds);
        s.time_ = seconds;
    }
    alSourceStop(al);
    alSourcei(al, AL_BUFFER, 0);
    voiceOwner_[s.voice_] = nullptr;
    s.voice_ = -1;
}

// the sources that play and are heard, the higher priority first, then the nearer, take the voices
void Context::assignVoices() {
    std::vector<Source *> wanted;
    for (auto &w : sources_) {
        std::shared_ptr<Source> s = w.lock();
        if (!s) {
            continue;
        }
        bool audible = s->state_ == Source::State::PLAYING && s->sound_ && !s->silenced_ && (!s->positional || s->distance_ <= s->maxDistance);
        if (audible) {
            wanted.push_back(s.get());
        } else if (s->voice_ >= 0) {
            releaseVoice(*s, true);
        }
    }
    std::stable_sort(wanted.begin(), wanted.end(), [](const Source *a, const Source *b) {
        if (a->priority != b->priority) {
            return a->priority > b->priority;
        }
        return a->distance_ < b->distance_;
    });
    const size_t n = std::min(wanted.size(), voices_.size());
    for (size_t i = n; i < wanted.size(); i++) {
        releaseVoice(*wanted[i], true);
    }
    for (size_t i = 0; i < n; i++) {
        Source &s = *wanted[i];
        if (s.voice_ >= 0) {
            apply(s, voices_[s.voice_]);
            continue;
        }
        for (size_t v = 0; v < voices_.size(); v++) {
            if (!voiceOwner_[v]) {
                bindVoice(s, (int)v);
                break;
            }
        }
    }
}

void Context::visit(scene::Node &node, const Mat4 &parent, float dt, uint32_t mask) {
    if ((node.appMask() & mask) == 0) {
        return;
    }
    Mat4 world = parent * node.matrix();
    if (auto *ss = dynamic_cast<SoundSource *>(&node)) {
        Source &s = *ss->source_;
        Vec3 p(world[3]);
        s.positional = true;
        s.position = p;
        s.direction = normalize(transformVector(world, {0, 1, 0}));
        s.velocity = ss->hasLast_ && dt > 0 ? (p - ss->last_) / dt : Vec3(0.0f);
        ss->last_ = p;
        ss->hasLast_ = true;
        s.inGraph_ = true;
    }
    if (listener_.following_ && listener_.follow_.lock().get() == &node) {
        listener_.set(Vec3(world[3]), transformVector(world, axes::kForward), transformVector(world, axes::kUp));
        listener_.hasPose_ = true;
    }
    for (size_t i = 0; i < node.children().size(); i++) {
        visit(*node.children()[i], world, dt, mask);
    }
}

void Context::update(scene::Node &root, float dt, uint32_t mask) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    for (auto &w : sources_) {
        if (std::shared_ptr<Source> s = w.lock()) {
            s->graphSeen_ = s->graphSeen_ || s->inGraph_;
            s->inGraph_ = false;
        }
    }
    listener_.hasPose_ = false;
    visit(root, Mat4(1.0f), dt, mask);
    for (auto &w : sources_) {
        if (std::shared_ptr<Source> s = w.lock()) {
            s->graphSeen_ = s->graphSeen_ || s->inGraph_;
            // a source of the graph that this update did not reach: its branch is out of the mask or out of the graph
            s->silenced_ = s->graphSeen_ && !s->inGraph_;
        }
    }
    update(dt);
}

void Context::update(float dt) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    // the listener: its node when it follows one that update(root) did not reach
    if (listener_.following_ && !listener_.hasPose_) {
        if (scene::NodePtr n = listener_.follow_.lock()) {
            const Mat4 &m = n->matrix();
            listener_.set(Vec3(m[3]), transformVector(m, axes::kForward), transformVector(m, axes::kUp));
        }
    }
    listener_.velocity_ = listener_.hasLast_ && dt > 0 ? (listener_.position_ - listener_.last_) / dt : Vec3(0.0f);
    listener_.last_ = listener_.position_;
    listener_.hasLast_ = true;
    alVector(AL_POSITION, listener_.position_, 0, true);
    alVector(AL_VELOCITY, listener_.velocity_, 0, true);
    Vec3 at = axes::toYUp(listener_.forward_);
    Vec3 up = axes::toYUp(listener_.up_);
    const ALfloat orientation[6] = {at.x, at.y, at.z, up.x, up.y, up.z};
    alListenerfv(AL_ORIENTATION, orientation);
    alListenerf(AL_GAIN, std::max(0.0f, listener_.gain));

    sources_.erase(std::remove_if(sources_.begin(), sources_.end(),
                                  [](const std::weak_ptr<Source> &w) {
                                      return w.expired();
                                  }),
                   sources_.end());
    for (auto &w : sources_) {
        std::shared_ptr<Source> s = w.lock();
        Source &src = *s;
        src.distance_ = src.positional ? length(src.position - listener_.position_) : 0.0f;
        if (src.state_ != Source::State::PLAYING || !src.sound_) {
            continue;
        }
        bool ended = false;
        if (src.voice_ >= 0) {
            ALint state = 0;
            alGetSourcei(voices_[src.voice_], AL_SOURCE_STATE, &state);
            ended = state == AL_STOPPED && !src.loop;
        } else if (src.restart_) {
            // just played: its time starts with its voice
        } else {
            // virtual: its time goes on
            src.time_ += (double)dt * std::max(0.01f, src.pitch);
            double length = src.sound_->seconds();
            if (src.time_ >= length) {
                if (src.loop && length > 0) {
                    src.time_ = std::fmod(src.time_, length);
                } else {
                    ended = true;
                }
            }
        }
        if (ended) {
            releaseVoice(src, false);
            src.state_ = Source::State::STOPPED;
            src.time_ = 0;
            src.finished_ = true;
        }
    }
    assignVoices();
    for (auto &w : sources_) {
        if (std::shared_ptr<Source> s = w.lock()) {
            s->restart_ = false;
        }
    }
    for (Stream *st : streams_) {
        Source &src = *st->source_;
        src.distance_ = src.positional ? length(src.position - listener_.position_) : 0.0f;
        apply(src, st->alSource_);
    }

    // the callbacks last, on a copy: a callback can play, stop or free sources
    std::vector<std::shared_ptr<Source>> done;
    for (auto &w : sources_) {
        std::shared_ptr<Source> s = w.lock();
        if (s && s->finished_) {
            s->finished_ = false;
            done.push_back(s);
        }
    }
    for (Stream *st : streams_) {
        st->source_->finished_ = false;
    }
    for (const std::shared_ptr<Source> &s : done) {
        if (s->onFinished) {
            s->onFinished(*s);
        }
    }
}

} // namespace r4::audio
