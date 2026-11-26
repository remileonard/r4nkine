# contrib/librealspace: the sound of neosc_r4 with r4nkine_audio

This folder holds code for libRealSpace (branch `r4nkine`). Move it into libRealSpace, then remove this folder from R4nkine. The design is in `docs/AUDIO.md` of R4nkine.

## Contents

| File | Function |
| --- | --- |
| `src/r4bridge/R4SoundFx.h`, `.cpp` | The sound effects in PCM: `renderSoundFx` renders an XMIDI effect off line with a `MusicDevice` (OPL or munt), and finds a loop without end. `decodeVoc` decodes a VOC. |
| `src/r4game/R4Mixer.h`, `.cpp` | `R4Mixer`, a `RSMixer` on `r4nkine_audio` (OpenAL Soft). The music is a stream. The effects of the game are sources in space. The VOC of the channels play at the listener. |
| `src/executables/r4soundplayer/` | `r4soundplayer`: a player of the sounds of the game, as `neosc_r4` plays them (refer to "Player"). |
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
| `src/engine/Mt32Synth.h`, `.cpp`, `Mt32EmuOutput.h` | `Mt32Synth::readRoms` reads the files of the ROM folder once, and `open(roms, rate)` starts a synth from them in memory (`mt32emu_add_rom_data`). `open(romDir, rate)` does the two. |
| `src/strike_commander/SCMission.h`, `.cpp` | `playEffect` and `playOrUpdateEffect` give the position to the mixer. `playOrUpdateEffect` takes the position of the emitter, not the distance. |
| `src/strike_commander/SCPlane.cpp`, `SCMissionActors.cpp` | The calls of `playOrUpdateEffect` give the position. |
| `src/r4bridge/CMakeLists.txt` | `R4SoundFx`. |
| `src/r4game/CMakeLists.txt` | `R4Mixer` and the link to `r4nkine::r4nkine_audio` when the target exists (`R4GAME_AUDIO`). |
| `src/r4game/R4Screen.cpp` | `R4Mixer::update()` at each frame, in `refresh`. |
| `src/r4game/R4ConfigPanel.cpp` | In the Sound group: Music Volume and Effects Volume (they apply at once), Effects Synthesizer and HRTF (at the next start). |
| `src/executables/neosc_r4/main.cpp` | `RSMixer::setInstance(std::make_unique<R4Mixer>())`. |
| `cmake/R4nkine.cmake` | `r4soundplayer`, when `r4nkine::r4nkine_audio` exists. |

## Player

`r4soundplayer [-data folder] [-hrtf]` shows the sounds of the game, with R4Mixer:

- The tab XMIDI: the effects of `SOUNDFX`, rendered into PCM with the synthesizer of `[Sound] midi_music`. The list gives the length and the loops.
- The tab VOC: the digitized effects of `DSOUNDFX.PAK`, with the names of `SoundEffectIds`.
- The tab Music: the tracks of a bank, played as in the game.
- For a sound: the curve of its level, Play, Stop, Loop, Gain, and Export WAV (the introduction and two passes of the loop).
- In space: a direction and a distance from the listener, or "Pass in front": the source goes from left to right at a speed, for the Doppler effect. Use headphones, and `-hrtf` to hear above and below.
- The volumes of the music and of the effects. The synthesizer of the effects (OPL or MT-32) and "Render the effects again".

The data: `./SC.DAT` and `./assets`, as `neosc_r4`. Else `-data`, a folder with `BIGTRE.TRE` and `LILTRE.TRE`.

## Settings (`[Sound]`)

| Key | Default | Function |
| --- | --- | --- |
| `max_voices` | 64 | The sources heard at one time. The farther sources are virtual. |
| `hrtf` | false | The HRTF of OpenAL Soft, for headphones. |
| `speed_of_sound` | 1125 | In units of the world a second (1125: feet). |
| `doppler` | 1 | The Doppler effect; 0 stops it. |
| `music_volume` | 100 | The volume of the music, 0 to 100. |
| `sfx_volume` | 100 | The volume of all the other sounds (effects, engine, radio, voices), 0 to 100. |
| `sfx_synth` | auto | The synthesizer that renders the XMIDI effects: `auto` (the one of the music), `opl` or `mt32`. |

`sfx_synth` selects the synthesizer of the XMIDI effects. Each effect is rendered on a new synthesizer:

- `opl`: `SOUNDFX.ADL` with the timbres of `STRIKE.AD`, on an OPL.
- `mt32`: `SOUNDFX.ROL` with the timbres of `STRIKE.MT`, on munt (MT-32 emulated). It needs the ROMs in `mt32_rom_dir`; they are read once, and each effect starts a new munt from them in memory. Without them, the effects are on the OPL.
- `auto`: the synthesizer of the music (`midi_music`). For 0 (AdLib) and 1 (General MIDI): the OPL. For 2 (MT-32 on a MIDI port, which cannot be recorded) and 3: munt, else the OPL.

When the files of the music are not those of the synthesizer of the effects, R4Mixer loads the effects of the other synthesizer.

The volumes are the volume groups of `r4nkine_audio`: the music stream is in the group `GROUP_MUSIC`, all the other sources in `GROUP_EFFECTS`. The game's own volumes (`setVolume`, `engine_volume`) stay, and are multiplied by them.

## Tests done

- The build of all the targets of libRealSpace with the patch, on Linux.
- The effects of `SOUNDFX.ADL` on the OPL (`LILTRE.TRE`): 14 effects, rendered in 0.5 to 0.9 s. 4 loop: `GUN_FIRE` (introduction 1 s, loop 4.42 s) and the effects 6, 7 and 13. They hold a note without end: their loop is found in the wave. At the join of each loop, the step between two samples is as small as the median step of the sound. The 10 other effects end (0.33 to 4.08 s).
- The TVFX voices keep a noise of 1 to 6 units after the end of an effect. Below 16 units, `renderSoundFx` takes it as silence.
- `decodeVoc`: the 31 VOC of `DSOUNDFX.PAK` (entry 22 is empty), and the 39 VOC of the cutscenes, which have the length that SDL_mixer gives.
- `renderSoundFx` on the 29 sequences of `AMUSIC.PAK`: 5 have a loop without end, and the loop is found.
- `R4Mixer` and `r4soundplayer` on the null device of OpenAL: the lists, Play, the loops, the pass in front, Export WAV, the volumes, the render of the effects again.

## Not tested

- The sound on a sound card: the tests used the null device of OpenAL. The WAV files were not heard.
- The MT-32 render (munt): there were no ROMs. Tested: a folder without valid ROMs; at the start the effects go on the OPL, and the player refuses MT-32 and keeps the effects of the OPL.
- `neosc_r4` in a mission.
