# contrib/librealspace: the sound of neosc_r4 with r4nkine_audio

This folder holds code for libRealSpace (branch `r4nkine`). Move it into libRealSpace, then remove this folder from R4nkine. The design is in `docs/AUDIO.md` of R4nkine.

## Contents

| File | Function |
| --- | --- |
| `src/r4bridge/R4SoundFx.h`, `.cpp` | The sound effects in PCM: `renderSoundFx` renders an XMIDI effect off line with a `MusicDevice` (OPL or munt), and finds a loop without end. `decodeVoc` decodes a VOC. |
| `src/r4game/R4Mixer.h`, `.cpp` | `R4Mixer`, a `RSMixer` on `r4nkine_audio` (OpenAL Soft). The music is a stream. The effects of the game are sources in space. The VOC of the channels play at the listener. |
| `patches/r4-audio.patch` | The changes to the files of libRealSpace (refer to "Patch"). |
| `.clang-format` | The format of the files. |

## Apply

From the root of libRealSpace, on the branch `r4nkine`:

```sh
cp -r <r4nkine>/contrib/librealspace/src/. src/
git apply <r4nkine>/contrib/librealspace/patches/r4-audio.patch
```

Configure with a R4nkine that has `r4nkine_audio` (the `audio` feature of vcpkg, OpenAL Soft). Without `r4nkine_audio`, `R4Mixer` is not built and `neosc_r4` keeps `RSMixer` and SDL_mixer.

## Patch

| File | Change |
| --- | --- |
| `src/engine/AILXmidiDriver.h`, `.cpp` | `endlessLoopStarts` and `endlessLoopJumps`: the driver counts the loops without end (`FOR_LOOP` with a count of 0) and the jumps of their `NEXT_LOOP`. |
| `src/engine/RSMixer.h`, `.cpp` | `setInstance`, as `SCRenderer`. The members are protected. The functions of the effects and of the channels are virtual. `init` is in two parts: `initEngine(rate)` makes the devices, and `init` opens SDL_mixer. New: `playEffectAt` and `playOrUpdateEffectAt` with the position. |
| `src/strike_commander/SCMission.h`, `.cpp` | `playEffect` and `playOrUpdateEffect` give the position to the mixer. `playOrUpdateEffect` takes the position of the emitter, not the distance. |
| `src/strike_commander/SCPlane.cpp`, `SCMissionActors.cpp` | The calls of `playOrUpdateEffect` give the position. |
| `src/r4bridge/CMakeLists.txt` | `R4SoundFx`. |
| `src/r4game/CMakeLists.txt` | `R4Mixer` and the link to `r4nkine::r4nkine_audio` when the target exists (`R4GAME_AUDIO`). |
| `src/r4game/R4Screen.cpp` | `R4Mixer::update()` at each frame, in `refresh`. |
| `src/executables/neosc_r4/main.cpp` | `RSMixer::setInstance(std::make_unique<R4Mixer>())`. |

## Settings (`[Sound]`)

| Key | Default | Function |
| --- | --- | --- |
| `max_voices` | 64 | The sources heard at one time. The farther sources are virtual. |
| `hrtf` | false | The HRTF of OpenAL Soft, for headphones. |
| `speed_of_sound` | 1125 | In units of the world a second (1125: feet). |
| `doppler` | 1 | The Doppler effect; 0 stops it. |

`midi_music` selects the synthesizer of the effects, as for the music:

- 0 (OPL) and 1 (General MIDI): an OPL.
- 3 (munt): a second munt, off line.
- 2 (MT-32 on a MIDI port): munt when the ROMs are in `mt32_rom_dir`. Else, the AdLib effects on an OPL.

## Tests done

- The build of all the targets of libRealSpace with the patch, on Linux.
- `decodeVoc`: the 39 VOC of `MID1VOC.PAK`, `MID33VOC.PAK`, `MID34VOC.PAK` and `MID36VOC.PAK` have the length that SDL_mixer gives.
- `renderSoundFx` on the 29 sequences of `AMUSIC.PAK`: 5 have a loop without end, and the loop is found (example: an introduction of 16 s and a loop of 34.6 s).
- `R4Mixer` on the null device of OpenAL: the effects, an emitter, a channel with a loop, the music stream, stop.

## Not tested

- The effects of `SOUNDFX.ADL` and `DSOUNDFX.PAK`: `SOUND.TRE` was not available for the tests. The render of the effects is not heard yet.
- The MT-32 render (munt): there were no ROMs.
- `neosc_r4` in a mission, with a sound card.
