# RPGFramework.PSXReverb

An Audio Mixer effect for Unity that reproduces the PlayStation's SPU reverb: the console's presets, its 22,050 Hz
processing in 16-bit integer maths, and its resampling filter, sample for sample. Any Unity project can use it; it
depends on nothing else in the RPG Framework.

**Not built yet.** This repository holds the native project the reverb will be written in.

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

## License

MIT — see `LICENSE.md`.
