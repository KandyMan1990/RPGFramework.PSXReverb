# RPGFramework.PSXReverb

An Audio Mixer effect for Unity that reproduces the PlayStation's SPU reverb: the console's presets, its 22,050 Hz
processing in 16-bit integer maths, and its resampling filter, sample for sample. Any Unity project can use it; it
depends on nothing else in the RPG Framework.

**Not a Unity plugin yet.** The reverb itself is built and tested, with a command-line tool to hear it; the Unity
plugin that wraps it is still to come.

---

## Layout

- `Native~/` — the reverb in C, with its tests, built with CMake. Unity ignores folders ending in `~`.
- The repository root is the Unity package. The built plugin will live here beside `package.json`.

---

## Building and testing

Needs CMake 3.21 or later and a C compiler (Xcode's on macOS, Visual Studio's on Windows, GCC or Clang on Linux).

```
cd Native~
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

`debug` builds with warnings as errors and with AddressSanitizer and UndefinedBehaviorSanitizer where the compiler has
them; `release` builds optimised. On macOS, `cmake --preset xcode` generates an Xcode project in `Native~/build/xcode`
for stepping through the code in a debugger.

---

## Hearing it

`psxreverb_render` runs a WAV through the reverb and writes a 16-bit stereo WAV, mixed with the input as the console's
mixer does, with four seconds added for the tail:

```
Native~/build/release/psxreverb_render --mode hall INPUT.wav OUTPUT.wav
```

`--mode` is one of `off`, `room`, `studio-a`, `studio-b`, `studio-c`, `hall`, `space`, `echo`, `delay` and `pipe`,
`studio-c` by default;
`--depth` sets the reverb's level, 0–127 (40); `--delay` and `--feedback` set echo and delay's, 0–127; `--tail`
changes the seconds added; `--wet` writes the reverb alone. `--help` lists them all.

It reads 8, 16, 24 and 32-bit integer and 32-bit float WAVs at any of the usual rates from 8 to 192 kHz, and writes
at the same rate. At 44.1 kHz, the reverb's own rate, it runs as the console's does, sample for sample. At other rates
it resamples to 44.1 kHz and back, flat across the reverb's band, which brings the reverb a fraction of a millisecond
later — 0.29 ms at 48 kHz.

---

## License

MIT — see `LICENSE.md`.
