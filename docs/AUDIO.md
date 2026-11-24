# Spatial audio: `r4nkine_audio`

This document gives the design of the audio of R4nkine. The design is not done yet.

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

- `audio::Sound` is a PCM buffer in OpenAL: 8 or 16 bits, mono or stereo, with its sample rate.
- Only a mono sound has a position in space. A stereo sound plays without position (music, menus).
- `audio::loadWAV(path)` reads a WAV file.
- `audio::Sound::fromPCM(samples, count, channels, rate)` makes a sound from samples that the application gives.
- Use a `Sound` for a sound that is known before it plays: a sound effect, a voice. For a sound that the application makes while it plays, use a stream (refer to "Streams").

## Sources

`audio::Source` plays a `Sound`. It can be outside the graph.

- A source without position plays at the listener: a voice, the radio, a menu. This is the default.
- A source can have a position, a speed and a direction that the application sets. Example: a program without a graph.
- `play()`, `pause()`, `stop()`, `isPlaying()`, and the position in the sound (`offset`).
- `gain`, `pitch` and `loop`, as for a sound source node.
- A source that is not playing uses no voice.

### End of a sound

- `Source::onFinished` is a callback. `audio::update` calls it when a source without `loop` comes to the end of its sound, or when `stop()` stops it.
- The callback runs on the thread of the application, during `audio::update`. It does not run on the audio thread. Thus, the callback can change the state of the application without a lock.
- The application uses `onFinished` and `isPlaying()` for its own logic. Example: libRealSpace frees one of its channels, as it does with `Mix_ChannelFinished` of SDL_mixer now.

## Streams

`audio::Stream` plays a sound that the application makes while it plays: music from a sequencer, a synthesizer.

- `audio::Stream(channels, rate, fill)` makes a stream. `fill(int16_t *out, int frames)` writes the next frames.
- The audio thread of `r4nkine_audio` calls `fill`. It keeps a queue of OpenAL buffers full. This is the same model as `Mix_HookMusic` of SDL_mixer.
- `fill` runs on the audio thread. The application must protect the data that `fill` reads with a lock.
- `Stream::setLatency(ms)` sets the length of the queue. A short queue gives a quick answer to a change of music. A long queue prevents gaps when a frame is slow.
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
| `maxDistance` | the distance after which the attenuation stops. After this distance, the source stops and frees its voice. |
| `rolloff` | the slope of the attenuation |
| `priority` | when there are not sufficient voices, the sources with a low priority stop first |
| `relative` | the source is in the space of the listener (a sound in the cockpit) |

- `play()`, `stop()`, `isPlaying()` and `onFinished` are those of its `audio::Source`.
- The world matrix of the node gives the position. The change of position between two APP traversals, divided by the time step, gives the speed. Thus, the Doppler effect needs nothing from the application.
- The direction of the node (its forward axis) can give a cone (`innerAngle`, `outerAngle`, `outerGain`). Example: the exhaust of an engine.

## Listener

- `audio::Context::listener()` gives the listener.
- By default, the listener follows a camera: its position, its forward axis and its up axis.
- `Listener::follow(node)` attaches the listener to a node. Example: the head of the pilot.
- In virtual reality, `Listener::setPose(session.head())` sets the pose of the head at each frame.
- The speed of the listener comes from its change of position, as for a source.

## Update during APP

- `audio::update(context, root, dt, mask)` goes through the graph below `root`. It uses the APP mask (`Node::appMask`). Thus, one mask stops all the sounds of a branch.
- For each `SoundSource`, it does these steps:
  1. It calculates the world matrix, the position and the speed.
  2. It calculates the distance to the listener.
  3. When the distance is more than `maxDistance`, it stops the source.
- Then it gives the voices to the sources that play: first by `priority`, then by distance. OpenAL Soft gives 256 voices by default. `Context::setMaxVoices` decreases this number.
- The sources without a voice are virtual. They keep their position in the sound. When a voice is free again, they continue from this position.
- It updates the sources outside the graph and the streams.
- It calls the `onFinished` callbacks of the sources that stopped since the last update.
- The application calls `audio::update` one time for each frame, after `scene::app` and before the draw. The position of the listener is then the position of the frame.

## Threads

- The thread of the application calls all the functions of `r4nkine_audio`, and `audio::update`.
- The audio thread of `r4nkine_audio` fills the streams only. It calls `Stream::fill`. It does not call `onFinished`.
- OpenAL Soft mixes the voices on its own thread.

## Context options

| Option | Function |
| --- | --- |
| `hrtf` | the HRTF of OpenAL Soft for headphones: the listener hears above and below |
| `doppler` | the strength of the Doppler effect; 0 stops it |
| `distanceModel` | inverse, linear or exponential, clamped or not |
| `masterGain` | the gain of all the sources |
| `device` | the name of the OpenAL device; empty for the default device |

## Use in libRealSpace

- libRealSpace is a client of `r4nkine_audio`. `R4Mixer` replaces `RSMixer` for `neosc_r4`, as `R4Screen` replaces `RSScreen`. The other programs keep `RSMixer` and SDL_mixer.
- `R4Mixer` keeps the sequencer, the AIL driver and the synthesis of `RSMixer`. Only the output changes.
- The music: one stereo `Stream`. Its `fill` calls the render of the synthesis (`RSMixer::render`, under its lock). This replaces `Mix_HookMusic`.
- The voices and the radio (VOC): a `Sound` for each, on a `Source` without position. `onFinished` frees the channel. This replaces `Mix_PlayChannel` and `Mix_ChannelFinished`.
- The sound effects: the game renders each sound effect of the AIL driver one time, at load, into a mono PCM buffer (`Sound::fromPCM`).
- Some effects loop. Some effects change during the flight, for example the engine. An inventory of the effects of the game must find them before the work starts. A loop uses `loop`. A change of speed uses `pitch`.
- `R4Strike` attaches a `SoundSource` to the node of each emitter: the engine of each plane, the guns, the missiles, the explosions. It replaces `RSMixer::playOrUpdateSoundFx(effect, emitter, distance)`.
- `referenceDistance` and `maxDistance` are set from the values of the game (1000 and 5000 units). Thus, the game sounds as before, with a direction.
- The music, the radio and the voices are not in space.
- Thus, `neosc_r4` opens one audio device only, and it does not need SDL_mixer.
- In virtual reality, the listener follows the head of `r4nkine_xr`.

## Sequence of work

1. `r4nkine_audio`: the context, the sounds, the WAV loader, `Source` with `onFinished`, `Stream` and its thread, the listener on a camera, `SoundSource`, `audio::update`. Tests without a device (the calculation of the positions, the speeds and the priorities).
2. r4fly: an option that plays a WAV file on a node of the model, to hear the position, the Doppler effect and the HRTF.
3. Virtual reality: the listener on the head.
4. libRealSpace: `R4Mixer` with the music in a `Stream` and the voices on sources without position. Then the inventory of the sound effects, their render into PCM buffers, and the sources on the nodes of `R4Strike`.
