# Audio: `sound.mgr`, `.wav`, `.mp3`

The engine played audio through Miles. The rebuild decodes the same files
itself and plays them with SDL3 audio streams.

Code: `src/audio/` (WAV parser, MP3 stream over dr_mp3), `src/engine/ini.cpp`,
`src/app/sound.cpp` (SDL output). Tests: `tests/test_audio.cpp`.

## Files

- `Harbinger.ini` `[Paths]` names every index; `Sounds` is `sound.mgr`
  (the `.mgr` format, see mgr.md), 445 entries.
- 409 entries are RIFF WAVE, 16-bit PCM, mostly mono 22050 Hz. 36 are MP3
  (music and long ambient loops, 44.1 kHz stereo).
- Entry 171, `Res\Common\Sounds\DreamCatcher.mp3`, is in no shipped archive.
- Menu music is `[Paths] IntroMusic` (`res\Common\Music\Intro.mp3`), looped.
  `LoadMusic` is a second key read by `FUN_004a6850`, not used by the rebuild yet.

## Interface sounds

An `.itf` interface carries a table of `sound.mgr` ids indexed by slot
(see itf.md). Controls ask their interface to play a slot.

| Slot | Use | startup.itf | new.itf |
|---|---|---|---|
| 9 | pointer enters an animated button; Options plays it as the sound check when the Sound slider moves | 374 `Load.wav` | 375 |
| 10 | animated button pressed | 373 `Exit.wav` | 376 |

Volumes come from `config.cfg` (docs/formats/config.md): music plays at
`music / 12`, sounds at `sound`.

## Evidence

| Claim | Label | Evidence |
|---|---|---|
| Index paths and `IntroMusic` come from `[Paths]` | proven | String `IntroMusic` at 0x57debc is used by `FUN_004a63c0`; `FUN_005138c0` reads `Fonts` the same way |
| Slots 9 / 10 are hover / press | proven | `GCtrlAnimatedBtn_v9` plays slot 9 on hover, `_v7` slot 10 on press, both through `FUN_004c64e0` |
| Playing a slot that is still sounding restarts it | likely | `FUN_004c6720` stops the slot's sample before starting it again |
| WAV / MP3 counts and formats | proven by data | `testRealData` decodes every `sound.mgr` entry |
| Menu music loops | guessed | Usual for a menu track; the loop flag in the Miles call is not traced yet |
