# Spatial audio: `r4nkine_audio`

This document gives the design of the audio of R4nkine. Steps 1 to 4 of "Sequence of work" are done. The code of steps 5 and 6 is in `contrib/librealspace`, to move into libRealSpace. The API is in `include/r4/audio/audio.h`.

The scene graph knows the position of each node, the camera, and the head in virtual reality. Thus, R4nkine can put the sounds in space. The module plays the sounds and puts them in space. The application makes the sounds. The OpenAL module of OpenSceneGraph (osgAL, then osgAudio) uses the same model.

`r4nkine_audio` is the only part that uses OpenAL Soft. An application, for example libRealSpace, uses the API of `r4nkine_audio` only. It does not include the headers of OpenAL.

## Reasons

- The game makes its own sounds, but it does not put them in space:
  - `RSMixer::distanceVolume` gives a volume from the distance only: 100 − d / 10 up to 1000 units, 5 up to 5000 units, then 0.
  - There is no direction, no stereo pan and no Doppler effect.
  - The game calculates the distance of each emitter with its own code.
- The graph has the world matrix of each node. Thus, the position and the speed of a sound source are available without more calculation.
- In virtual reality, the listener must follow the head at the rate of the render. `r4nkine_xr` has the pose of the head.
- Other programs can use the module, for example r4fly.

## Limits

| In `r4nkine_audio` | Not in `r4nkine_audio` |
| --- | --- |
| The OpenAL device and context | The synthesis of the sounds (for libRealSpace: the XMIDI sequencer, the AIL driver, the OPL3 and MT-32 emulators) |
| The sounds (PCM buffers), the sources, the streams | The logic of the channels of the application |
| The sound sources as nodes of the graph | A mixer with effects (reverb, filters) |
| The listener | The decoders of compressed formats (MP3, Ogg) |
| The update during the APP traversal, the end of the sounds | An emulation of the API of SDL_mixer |
| The Doppler effect, HRTF, the attenuation with the distance | |
| The limit of the voices and their priority | |
| The thread that fills the streams | |
| A WAV loader, as a convenience | |

## Library and dependency

- `r4nkine_audio` is a separate library, as `r4nkine_xr`. Only the programs that play spatial sound link OpenAL.
- The dependency is OpenAL Soft, the `audio` feature of `vcpkg.json`, and the CMake option `R4_BUILD_AUDIO`.
- OpenAL Soft has the LGPL license. Link it dynamically (a DLL on Windows).
- Without the module, R4nkine does not change.

## Axes and units

- OpenAL is right-handed, with Y up. The world of R4nkine is Z up (`r4/axes.h`).
- The module changes the positions, the speeds and the orientations from the world of R4nkine to the axes of OpenAL. The application does not do this change.
- The units are the units of the world. `Context::setSpeedOfSound` sets the speed of sound in these units. Example: 343 for meters, 1125 for feet.

## Sound data

- `audio::Sound` is a PCM buffer of 16 bits in OpenAL, mono or stereo, with its sample rate. Make the sounds after the context. Free them before it.
- Only a mono sound has a position in space. A stereo sound plays without position (music, menus).
- `audio::loadWAV(path)` reads a WAV file of PCM, 8 or 16 bits, mono or stereo.
- `audio::Sound::fromPCM(samples, count, channels, rate)` makes a sound from samples that the application gives.
- Use a `Sound` for a sound that is known before it plays: a sound effect, a voice. For a sound that the application makes while it plays, use a stream (refer to "Streams").

## Sources

`audio::Source` plays a `Sound`. It can be outside the graph. `Context::createSource(sound)` makes it. Free the sources before the context.

- A source without position plays at the listener: a voice, the radio, a menu. This is the default.
- A source can have a position, a speed and a direction that the application sets. Example: a program without a graph.
- `play()`, `pause()`, `stop()`, `isPlaying()`, and the position in the sound (`offset`).
- `gain`, `pitch` and `loop`, as for a sound source node.
- A source that is not playing uses no voice.

### End of a sound

- `Source::onFinished` is a callback. `Context::update` calls it when a source without `loop` comes to the end of its sound, or when `stop()` stops it.
- The callback runs on the thread of the application, during `Context::update`. It does not run on the audio thread. Thus, the callback can change the state of the application without a lock.
- The application uses `onFinished` and `isPlaying()` for its own logic. Example: libRealSpace frees one of its channels, as it does with `Mix_ChannelFinished` of SDL_mixer now.

## Streams

`audio::Stream` plays a sound that the application makes while it plays: music from a sequencer, a synthesizer.

- `Context::createStream(channels, rate, fill, latencyMs)` makes a stream. `fill(int16_t *out, int frames)` writes the next frames.
- The audio thread of `r4nkine_audio` calls `fill`. It keeps a queue of OpenAL buffers full. This is the same model as `Mix_HookMusic` of SDL_mixer.
- `fill` runs on the audio thread. The application must protect the data that `fill` reads with a lock.
- `latencyMs` sets the length of the queue (4 buffers). A short queue gives a quick answer to a change of music. A long queue prevents gaps when a frame is slow.
- A stream has a `Source`: its gain, its `play`, `pause` and `stop`. A mono stream can have a position, as any source.

## Sound source node

`scene::SoundSource` is a node of the graph. It has no children. It contains an `audio::Source` (or the source of a stream). The node gives it its position, its speed and its direction.

| Property | Function |
| --- | --- |
| `sound` | the `audio::Sound` that the source plays |
| `gain` | 0 to 1 and more |
| `pitch` | 1 is the speed of the data. Use it for an engine with a variable speed. |
| `loop` | the sound starts again at its end |
| `referenceDistance` | the distance where the gain is 1 |
| `maxDistance` | the distance after which the attenuation stops. After this distance, the source has no voice. Its time goes on. |
| `rolloff` | the slope of the attenuation |
| `priority` | when there are not sufficient voices, the sources with a low priority lose their voice first |
| `positional` | false: the source plays at the listener (a voice, the radio, a sound in the cockpit). A `SoundSource` sets it to true. |

- `play()`, `stop()`, `isPlaying()` and `onFinished` are those of its `audio::Source`.
- The world matrix of the node gives the position. The change of position between two APP traversals, divided by the time step, gives the speed. Thus, the Doppler effect needs nothing from the application.
- The direction of the node (its forward axis) can give a cone (`innerAngle`, `outerAngle`, `outerGain`). Example: the exhaust of an engine.

## Listener

- `audio::Context::listener()` gives the listener.
- `Listener::fromView(camera.view)` puts the listener at a camera of the renderer: its position, its forward axis and its up axis. `Listener::fromCameraPose(worldFromCamera)` does the same from the pose of a camera.
- `Listener::setPose(position, forward, up)` sets the pose directly. Example: a camera of an application that has its own camera.
- `Listener::follow(node)` attaches the listener to a node. Example: the head of the pilot.
- In virtual reality, `Listener::fromCameraPose(session.head())` sets the pose of the head at each frame.
- The speed of the listener comes from its change of position, as for a source.

## Update during APP

- `Context::update(root, dt, mask)` goes through the graph below `root`. It uses the APP mask (`Node::appMask`). Thus, one mask stops all the sounds of a branch.
- For each `SoundSource`, it does these steps:
  1. It calculates the world matrix, the position and the speed.
  2. It calculates the distance to the listener.
  3. When the distance is more than `maxDistance`, the source has no voice.
- A `SoundSource` in a branch out of the mask, or out of the graph, has no voice.
- Then it gives the voices to the sources that play: first by `priority`, then by distance. `Settings::maxVoices` sets the number of voices (64 by default). The streams have their own voices.
- The sources without a voice are virtual. They keep their position in the sound. When a voice is free again, they continue from this position.
- It updates the sources outside the graph and the streams.
- It calls the `onFinished` callbacks of the sources that stopped since the last update.
- `Context::update(dt)` does the same without a graph.
- The application calls `Context::update` one time for each frame, after `scene::app` and before the draw. The position of the listener is then the position of the frame.

## Threads

- The thread of the application calls all the functions of `r4nkine_audio`, and `Context::update`.
- The audio thread of `r4nkine_audio` fills the streams only. It calls `Stream::fill`. It does not call `onFinished`.
- OpenAL Soft mixes the voices on its own thread.

## Context options

| Option | Function |
| --- | --- |
| `Settings::device` | the name of the OpenAL device; empty for the default device |
| `Settings::hrtf` | the HRTF of OpenAL Soft for headphones: the listener hears above and below |
| `Settings::maxVoices` | the sources heard at one time |
| `Context::setDoppler` | the strength of the Doppler effect; 0 stops it |
| `Context::setDistanceModel` | inverse, linear or exponential, clamped or not |
| `Context::setSpeedOfSound` | in the units of the world |
| `Listener::gain` | the gain of all the sources |

`Context::createLoopback(rate)` makes a context without a device. `Context::render` gives its stereo output in memory. The tests use it: they need no sound card. OpenAL Soft dithers its 16 bits output: silence is a noise of ±1.

## Use in libRealSpace

libRealSpace is a client of `r4nkine_audio`. `R4Mixer` replaces `RSMixer` for `neosc_r4`, as `R4Screen` replaces `RSScreen`. The other programs keep `RSMixer` and SDL_mixer.

### Synthesis modes

`[Sound] midi_music` selects the synthesis. The mode tells where the sound is made:

| Mode | Music | XMIDI sound effects |
| --- | --- | --- |
| 0: OPL (default) | OPL, in the program | the same OPL |
| 1: General MIDI | a MIDI port, or a SoundFont in the program | a separate OPL (`sfxOpl`) |
| 2: MT-32 hardware | the MIDI port | the same MT-32 |
| 3: MT-32 emulated (munt) | munt, in the program | the same munt |

The digitized sounds (VOC) do not use a synthesizer.

### Music

- The music is one stereo `Stream`. Its `fill` calls `RSMixer::render` under `engineMutex`. This replaces `Mix_HookMusic`.
- The stream contains only the sound that the program makes: modes 0 and 3, and mode 1 with a SoundFont. When the music goes to a MIDI port (mode 2, mode 1 with a port), it does not go through `r4nkine_audio`.
- The music is not in space.

### Sound effects

All the sound effects are PCM `Sound`s on positional `Source`s. Thus, all the effects are in space, in all the modes.

- A VOC effect is decoded into a mono PCM buffer. `R4Mixer` has its own VOC decoder (8 bits, unsigned, blocks of type 1), because SDL_mixer is not used.
- An XMIDI effect of `SOUNDFX.ADL` is rendered into a mono PCM buffer at load, one time for each bank:
  1. A new synthesizer and a new AIL driver render the effect. They do not share voices with the music.
  2. An effect without a loop, or with a loop that has a count, is rendered up to `SEQ_DONE`, then up to silence (the release; the TVFX voices keep a noise of a few units, which is silence). The maximum length is 10 seconds.
  3. An effect with a loop without end (`FOR_LOOP`, controller 116, with a count of 0) is rendered with its introduction and two passes of the loop. The driver gives the sample of `FOR_LOOP` and of the jump of `NEXT_LOOP` (controller 117). The second pass is the body of the loop: the state of the synthesizer is then stable (voices in release, TVFX curves), and the end joins the start.
  4. The buffer is the introduction and the body, with loop points. The source plays the introduction one time, then loops on the body.
  5. An effect that holds a note without end, without a loop in its sequence (`GUN_FIRE` on the OPL), is still heard after 6 seconds. Its loop starts at 1 s. Its end is in the last second, where the wave is the most like the wave at 1 s.
- The synthesizer of the render follows the mode:
  - modes 0 and 1: an OPL;
  - mode 3: one munt, off line. The timbres of `STRIKE.MT` go to munt before the render, and munt must process the sysex before the first note;
  - mode 2: the output of the hardware cannot be captured. Use munt when the ROMs are there, else the OPL.
- The game stops an effect while it plays in two cases: its volume is 0 (it is too far to hear), and the gun stops (`SCMission::stopEffect`). In the second case, the source stops at once, without the release of the synthesizer.
- `setRelVolume` becomes the gain of the source. During an effect, only its volume changes. Thus, the PCM buffer is correct.
- The renders can be kept in a cache on the disk.

### Sources of the effects

- `SCMission::playEffect(id, position)` makes a source at the position. The source is released at the end of its sound.
- `SCMission::playOrUpdateEffect(id, emitter, distance)` keeps one source for each emitter and effect. It updates its position at each frame. Later, `R4Strike` can attach a `SoundSource` to the node of the actor.
- There is no limit of 5 channels: all the effects are heard. `Settings::maxVoices` (64) is the limit. A configuration key sets it, for example `[Sound] max_voices`.
- All the effects have the same priority. Thus, the distance gives the order: the sounds of the player are at the camera and keep their voices. The voices and the radio play without position: their distance is 0.
- The attenuation uses `LINEAR_CLAMPED` with `maxDistance` 1000: this is the rule 100 − d / 10 of the game.

### Voices and radio

- The voices and the radio (VOC) use a `Sound` on a `Source` without position. `onFinished` frees the channel. This replaces `Mix_PlayChannel` and `Mix_ChannelFinished`.

### Listener

- The listener follows the camera of `R4Renderer` (`Listener::fromView`). In virtual reality, it follows the head of `r4nkine_xr`.
- The positions of the sources and of the listener use the same frame as the render.
- The speed of sound is set in the units of the game, for the Doppler effect.

Thus, `neosc_r4` opens one audio device only, and it does not need SDL_mixer.

## Sequence of work

1. `r4nkine_audio`: the context, the sounds, the WAV loader, `Source` with `onFinished`, `Stream` and its thread, the listener on a camera, `SoundSource`, `Context::update`. Tests on a loopback context (`tests/audio_tests.cpp`): left and right, the listener on a node and on a camera, the end of a sound, the voices and their priority, the APP mask, the streams, the Doppler effect, the audio thread, WAV. **Done.**
2. r4fly: `-sound file.wav` plays a sound that turns about the models, heard from the camera; `-hrtf`. **Done.**
3. Virtual reality: the listener on the head (`fromCameraPose(session.head())`, in r4fly with `-vr`). **Done.**
4. `r4nkine_audio`: loop points in `Sound` (`AL_SOFT_loop_points`): a sound plays its introduction one time, then loops on its body. `Listener::setPose`. **Done.**
5. libRealSpace, `R4Mixer`: the output through `r4nkine_audio` for all the modes. The music in a `Stream`, the voices and the radio on sources without position, the VOC decoder. **Code in `contrib/librealspace`** (`R4Mixer`, `patches/r4-audio.patch`). The VOC decoder gives the lengths of SDL_mixer.
6. libRealSpace: the render of the XMIDI effects into PCM buffers (the loops, the synthesizer of each mode), the effects on positional sources, the listener on the camera. **Code in `contrib/librealspace`** (`R4SoundFx` in r4bridge, `R4Mixer`). The 14 effects of `SOUNDFX.ADL` are rendered on the OPL; 4 hold a note without end, and their loop is found in the wave. `r4soundplayer` plays the effects, the VOC and the music. Not tested: a sound card, the MT-32 ROMs. Refer to `contrib/librealspace/README.md`.
