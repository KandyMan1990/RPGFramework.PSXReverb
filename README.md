# RPGFramework.PSXReverb

An Audio Mixer effect for Unity that reproduces the PlayStation's SPU reverb: the console's ten presets, its 22,050 Hz
processing in 16-bit integer maths, and its resampling filter. At a 44.1 kHz output it computes the reverb sample for
sample as the best-tested emulation does; published hardware measurements put even that about 24 LSB from a real
console on reverb-heavy sound. Any Unity 6.6 project can use it; it depends on nothing else in the RPG Framework.

The plugin, `audioplugin_psxreverb`, is built for macOS (universal), Windows (x64, Arm64 and x86), Linux (x64),
Android (arm64-v8a, armeabi-v7a and x86_64) and iOS (devices, and the Simulator on Apple silicon and Intel Macs), and
committed to `Runtime/Plugins/` by CI, which also loads each desktop plugin as Unity does and tests it. It has run in
Unity 6.6 on macOS, in the iOS Simulator with either Xcode project type, and on an Android 15 emulator with 16 KB
pages; it has not yet run in Unity on Windows, Linux or an iOS device. WebGL takes no native audio plugins and the
consoles need their SDKs, so neither has it. Where the plugin is missing, Unity logs that PSX Reverb cannot be found
and the group passes what it receives straight through.

On iOS the plugin registers itself with Unity as the app starts, in `Runtime/Plugins/iOS/PSXReverbRegistration.mm`, with
nothing to add to the Xcode project. **Unity as a Library is not supported**: it starts Unity without the launch step
the registration waits for, so the mixer would report that PSX Reverb cannot be found.

---

## Using it

Add the package from its git URL in the Package Manager, naming a release:

```
https://github.com/KandyMan1990/RPGFramework.PSXReverb.git#v1.0.0
```

**PSX Reverb writes the reverb alone**, in place of what its group receives, as the console adds its reverb to the dry
sound rather than replacing it. So it belongs on a group of its own: a **Receive** followed by **PSX Reverb**, fed by
**Send** effects on the groups that should reverberate. Each Send's level is how much of its group reaches the reverb,
where the console turned reverb on or off per voice.

| Parameter | Range | Default | |
| --- | --- | --- | --- |
| Preset | 0–9 | 4 | 0 off, 1 room, 2–4 studio A–C, 5 hall, 6 space, 7 echo, 8 delay, 9 pipe. A different preset clears the reverb, cutting its tail; the same one again changes nothing. |
| Depth | 0–127 | 40 | The reverb's output volume, as the console's tools give it. |
| Delay | 1–127 | 127 | Echo and delay's delay time: about 5.85 ms a step, so a repeat every 743 ms at 127. Other presets ignore it. |
| Feedback | 0–127 | 127 | Echo's feedback: each repeat is Feedback / 127 of the one before, so 64 halves it. 127, the console's own, flips the sign and barely decays, ringing on for minutes. Other presets ignore it: delay with feedback is echo. |

Values are rounded to whole numbers, and held to their range. The Inspector names the preset in a dropdown above
Unity's sliders, says when the preset ignores Delay or Feedback, and warns when the effect is playing silence. To set a
parameter from a script, expose it from its slider's context menu and call `AudioMixer.SetFloat`.

**Change Delay before the echo sounds, not while it rings.** A new Delay moves where the echo reads in what it holds
without clearing it, so the join clicks, and clicks again on every repeat until the echo has died away. Choosing a
different preset clears it, and the new Delay starts clean.

Some suggestions for fitting it in:

- **One reverb group for the whole mix**, as the console had one reverb unit: music and sound effects each send to it
  at levels of their own, and the preset and depth are the room they share.
- **Set the preset and depth per place or per song**, from a script through their exposed parameters, at a change of
  scene or song where you can, since a new preset cuts the tail of what was ringing.
- **Treat Depth as how much reverb the place has** — 40, the default, suits most music — and the Sends as how much of
  each sound goes in.

It runs at the rate Unity's mixer does, from 8 to 192 kHz. At 44.1 kHz it is the console's reverb sample for sample; at
any other rate it resamples to 44.1 kHz and back, flat across the reverb's band, and the reverb arrives a little later
than on the console: 0.29 ms at 48 kHz and above, 0.66 ms at 32 kHz, and 3 to 8 ms from 22.05 kHz down. A rate it
cannot run at plays silence. Unity makes the effect again when its output rate changes.

---

## In the RPG Framework

Nothing in the framework needs it, and [RPGFramework.Audio](https://github.com/KandyMan1990/RPGFramework.Audio) picks
it up when it is installed:

- **A music asset gains Reverb settings**, a preset and a volume, each set when the song starts, and the music player's
  `SetReverbPreset` and `SetReverbVolume` set either directly. The volume, 0 to 1, goes to Depth in a straight line.
  Audio writes them through Preset and Depth exposed as `ReverbPreset` and `ReverbDepth`.
- **Field scripts set them** with `SET_REVERB_PRESET` and `SET_REVERB_VOLUME`.
- **The framework's mixer puts it on its Reverb bus**, fed by the music's and sound effects' reverb sends and by the
  music's echo.
- Without it, Audio hides the settings and writes nothing to the mixer, keeping a song's settings so they come back if
  it is installed again.

---

## Layout

- `Native~/` — the reverb in C, the Unity plugin around it, a command-line tool and the tests, built with CMake. Unity
  ignores folders ending in `~`. `Native~/include/psx_reverb.h` is the reverb's C interface, which any other host
  could use too.
- The repository root is the Unity package. The built plugins are in `Runtime/Plugins/<platform>/`, each with a
  `.meta` naming its platform and CPU; the `.meta` files are kept in `Native~/unity/package/` and installed beside the
  plugins.
- `Editor/` — the effect's Inspector, and a fix for Unity listing its mixer effects before it loads the plugin, which
  otherwise leaves PSX Reverb out of **Add Effect** after the editor starts until scripts next reload.

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
`android-arm64-v8a`, `android-armeabi-v7a` and `android-x86_64` (Android 8.0 and later, 16 KB pages; needs the NDK in
`ANDROID_NDK_HOME`), and `ios` and `ios-simulator` (iOS 15 and later; needs macOS and Xcode). Installing with the
package root as the prefix puts the plugin and its `.meta` files in place, over the committed one, for trying a change
in Unity:

```
cd Native~
cmake --preset macos
cmake --build --preset macos
cmake --install build/macos --prefix ..
```

After replacing a plugin, restart Unity: it never unloads a native plugin once loaded.

The committed plugins come only from CI. The **Plugins** workflow builds every platform on each push. To update the
committed plugins, run it from the Actions tab with **Commit the built plugins** ticked: it commits whatever changed in
`Runtime/` to the branch it ran on. The builds are reproducible, so a run with nothing changed commits nothing.

---

## Hearing it

`psxreverb_render` runs a WAV through the reverb and writes a 16-bit stereo WAV at the same rate, mixed with the input
as the console's mixer does, with four seconds added for the tail:

```
Native~/build/release/psxreverb_render --mode hall INPUT.wav OUTPUT.wav
```

On Windows the release build puts it in `Native~\build\release\Release\`.

`--mode` is one of `off`, `room`, `studio-a`, `studio-b`, `studio-c`, `hall`, `space`, `echo`, `delay` and `pipe`,
`studio-c` by default; `--depth` sets the reverb's level, 0–127 (40); `--delay` sets echo and delay's delay time,
1–127 (127), and `--feedback` echo's feedback, 0–127 (127), delay with feedback being echo; `--tail` changes the
seconds added; `--wet` writes the reverb alone. `--help` lists them all.

It reads 8, 16, 24 and 32-bit integer and 32-bit float WAVs, mono, stereo or more channels, of which it takes the first
two, at the same rates as the effect, and says how much later than the console the reverb arrives when it resamples.

---

## License

MIT — see `LICENSE.md`. `Native~/unity/sdk/AudioPluginInterface.h` is Unity's, under its own MIT licence beside it.
