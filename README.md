# RPGFramework.PSXReverb

An Audio Mixer effect for Unity that reproduces the PlayStation's SPU reverb: the console's presets, its 22,050 Hz
processing in 16-bit integer maths, and its resampling filter, sample for sample. Any Unity project can use it; it
depends on nothing else in the RPG Framework.

The plugin, `audioplugin_psxreverb`, is built for macOS (universal), Windows (x64, Arm64 and x86), Linux (x64),
Android (arm64-v8a, armeabi-v7a and x86_64) and iOS (devices, and the Simulator on Apple silicon and Intel Macs), and
committed to `Runtime/Plugins/` by CI. It has run in Unity 6.6 on macOS, in the iOS Simulator with either Xcode
project type, and on Android 15 with 16 KB pages. Consoles need their SDKs.

On iOS the plugin registers itself with Unity as the app starts, in `Runtime/Plugins/iOS/PSXReverbRegistration.mm`, with
nothing to add to the Xcode project. **Unity as a Library is not supported**: it starts Unity without the launch step
the registration waits for, so the mixer would report that PSX Reverb cannot be found.

---

## Layout

- `Native~/` — the reverb in C and the Unity plugin around it, with their tests, built with CMake. Unity ignores
  folders ending in `~`.
- The repository root is the Unity package. The built plugins are in `Runtime/Plugins/<platform>/`, each with a
  `.meta` naming its platform and CPU; the `.meta` files are kept in `Native~/unity/package/` and installed beside the
  plugins.
- `Editor/` — the effect's Inspector: the preset by name above Unity's sliders, a note on which settings the preset
  ignores, and a warning whenever the effect plays silence.

---

## Building and testing

Needs CMake 3.21 or later and a C and C++ compiler (Xcode's on macOS, Visual Studio's on Windows, GCC or Clang on
Linux).

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

## The plugins

Each platform has a preset that builds the plugin as the package carries it: `macos` (universal, macOS 12 and later),
`windows-x64`, `windows-arm64` and `windows-x86` (the C runtime linked in), `linux` (x64, the C++ runtime linked in),
`android-arm64-v8a`, `android-armeabi-v7a` and `android-x86_64` (Android 8.0 and later, 16 KB pages; needs
`ANDROID_NDK_HOME`), `ios` and `ios-simulator` (iOS 15 and later). Installing with the package root as the prefix puts the plugin and its `.meta` files in
place:

```
cd Native~
cmake --preset macos
cmake --build --preset macos
cmake --install build/macos --prefix ..
```

After replacing a plugin, restart Unity: it never unloads a native plugin once loaded.

The **Plugins** workflow builds every platform on each push. To update the committed plugins, run it from the Actions
tab with **Commit the built plugins** ticked: it commits whatever changed in `Runtime/` to the branch it ran on.

---

## Hearing it

`psxreverb_render` runs a WAV through the reverb and writes a 16-bit stereo WAV, mixed with the input as the console's
mixer does, with four seconds added for the tail:

```
Native~/build/release/psxreverb_render --mode hall INPUT.wav OUTPUT.wav
```

`--mode` is one of `off`, `room`, `studio-a`, `studio-b`, `studio-c`, `hall`, `space`, `echo`, `delay` and `pipe`,
`studio-c` by default; `--depth` sets the reverb's level, 0–127 (40); `--delay` sets echo and delay's delay time,
1–127, and `--feedback` echo's feedback, 0–127, delay with feedback being echo; `--tail` changes the seconds added;
`--wet` writes the reverb alone. `--help` lists them all.

It reads 8, 16, 24 and 32-bit integer and 32-bit float WAVs at any of the usual rates from 8 to 192 kHz, and writes
at the same rate. At 44.1 kHz, the reverb's own rate, it runs as the console's does, sample for sample. At other rates
it resamples to 44.1 kHz and back, flat across the reverb's band, which brings the reverb a fraction of a millisecond
later — 0.29 ms at 48 kHz.

---

## License

MIT — see `LICENSE.md`. `Native~/unity/sdk/AudioPluginInterface.h` is Unity's, under its own MIT licence beside it.
