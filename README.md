# ASPHALT 6 — PS Vita Port

<p align="center">
  <img src="extras/livearea/pic0.png" width="700" alt="Asphalt 6 PS Vita Banner" />
</p>

<p align="center">
  <b>Native port of Asphalt 6: Adrenaline (Gameloft) for PlayStation Vita and PlayStation TV.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-PS%20Vita%20%7C%20PS%20TV-003791.svg?style=flat-square&logo=playstation" alt="Platform PS Vita" />
  <img src="https://img.shields.io/badge/Title%20ID-ASPHALT06-ff69b4.svg?style=flat-square" alt="Title ID ASPHALT06" />
  <img src="https://img.shields.io/badge/Engine-Gameloft%20Glitch-brightgreen.svg?style=flat-square" alt="Engine" />
  <img src="https://img.shields.io/badge/Renderer-vitaGL%20%28GLES%202.0%2FGLSL%29-orange.svg?style=flat-square" alt="Renderer" />
  <img src="https://img.shields.io/badge/Status-Playable%20(Alpha)-yellow.svg?style=flat-square" alt="Status: Playable (Alpha)" />
</p>

---

## 📖 Description

**Asphalt 6: Adrenaline** is Gameloft's arcade racing game, originally released for Android as
`Asphalt-6-Adrenaline-v1.3.3-offline.apk` (package `com.gameloft.android.ANMP.GloftA6HP`). This
port runs the compiled native library (`libasphalt6.so` — Gameloft's proprietary **"Glitch"**
engine, derived from Irrlicht, with `gameswf` driving the Flash-based UI and `vox` handling
audio) directly on the PS Vita's ARM Cortex-A9 processor, using a dynamic loader (*soloader*)
and an Android environment emulation layer (*FalsoJNI*), with
[vitaGL](https://github.com/Rinnegatamante/vitaGL) providing the GLES 2.0/GLSL rendering
backend — the engine compiles its shaders at runtime, there is no fixed-function path.

The real game library is not shipped inside the visible APK: it lives inside a hidden,
already-"licensed" archive (`copy.inject`) bundled in the offline APK release, alongside
`pack.info` and the save-data defaults. See [`PORTING_PLAN.md`](PORTING_PLAN.md) for the full
engine-detection write-up.

### 🎮 Current Status: Playable (Alpha)

The game **boots to the main menu and races are playable**, but this is an early port with
open, actively-tracked bugs rather than a polished release. See
[`port_progress.md`](port_progress.md) for the full bug-by-bug diagnosis log (every fix is
backed by a real console log/crash-dump — no guessing).

### ✨ What Works

- **Native ARM Execution**: `libasphalt6.so` (armeabi-v7a) runs directly on the Vita's CPU via
  the soloader — no interpretation/emulation of game code.
- **Boots to Title + Main Menu**: full JNI lifecycle bootstrap through `SO relocated` →
  `FalsoJNI initialized` → the Flash-based UI (`gameswf`) → `GS_MenuMain`, presenting frames
  continuously.
- **vitaGL Graphics Pipeline**: GLES 2.0 with the engine's GLSL shaders compiled in caching at
  runtime (via `vitaShaRK`), plus a set of speedhacks tuned specifically for this engine (see
  `CMakeLists.txt` and [`README VITAGL.md`](README%20VITAGL.md) for the full flag reference).
- **Touch Input**: the front touch panel is mapped 1:1 to the engine's own touch UI
  (`GLGame_nativeTouchPressed/Moved/Released`) — menus and in-race steering/controls work via
  touch.
- **Intro FMV**: the intro video plays through a software FFmpeg decoder.
- **Assets from `ux0:`**: resource loading reads the game's packed/loose asset files from
  `ux0:data/asphalt6/data/`.
- **Extensive on-device diagnostics**: a watchdog thread that reports live thread state and
  frame pacing, a breadcrumb ring buffer for the last intercepted calls, and a unified,
  spam-collapsing log sink — all built specifically to diagnose this engine without guessing
  (see `source/utils/watchdog.c`, `source/utils/breadcrumb.c`, `source/utils/logger.c`).

### ⚠️ Known Issues

- **In-race pause menu is broken**: the engine looks up its own pause-menu buttons
  (`menu_main`, `back_btn_main`, `custom_controls_btn`) by name and doesn't find them in this
  build's HUD/profile variant, so their visibility never updates correctly. A crash-avoidance
  guard is in place (Bugs #026–#028), but the menu itself still needs its underlying cause
  fixed — see `port_progress.md`.
- **Elements intermittently disappear during races** (including the player's car): confirmed
  via on-device diagnostics to correlate with real, CPU-bound multi-second stalls inside the
  engine's mesh-batching code (`CBatchDriver::thisAppendBatch`). Root cause narrowed down;
  actual fix still pending a dedicated reverse-engineering pass (see the Log 047 entry in
  `port_progress.md`).
- **Graphical glitch on vehicle windows**: caused by a car-reflection texture
  (`*_Fixed.PVRTC4.tga`) that was never extracted from the original APK data — a missing-asset
  issue, not a code bug.
- **No audio yet**: the engine's audio layer (`vox::DriverAndroid`) talks to
  `android/media/AudioTrack` through raw JNI calls that this port doesn't intercept yet.
- **No physical button mapping**: only the touchscreen is wired up right now; D-Pad/analog/face
  buttons aren't mapped to menu or in-race actions.

---

## 📋 Prerequisites

To run this port on your PS Vita or PS TV, you will need:

1. A PS Vita / PS TV console running Custom Firmware (**HENkaku** or **Enso**),
   firmware 3.60/3.65 or later recommended.
2. [**kubridge**](https://github.com/TheOfficialFloW/kubridge/releases) and
   [**FdFix**](https://github.com/TheOfficialFloW/FdFix/releases) installed as
   kernel plugins (`ur0:tai/config.txt` under `*KERNEL`).
3. [**libshacccg.suprx**](https://github.com/Rinnegatamante/ShaRKBR33D/releases/latest)
   installed in `ur0:data/` (required — the engine compiles its GLSL shaders at runtime).
4. A legally obtained copy of **Asphalt 6: Adrenaline** offline release
   (`Asphalt-6-Adrenaline-v1.3.3-offline.apk`, package `com.gameloft.android.ANMP.GloftA6HP`).

---

## 📦 Installation Instructions

1. Install the `asphalt6.vpk` file on your console using **VitaShell**.
2. On your PC, place `Asphalt-6-Adrenaline-v1.3.3-offline.apk` in the project root (or extract
   it into `asphalt6_extract/`).
3. Use **psvita-port-toolkit** (the standalone tool this port is managed with) to extract
   `libasphalt6.so` from the APK's hidden `copy.inject` archive and prepare the asset files —
   open the toolkit and select "Continuar con un port existente" pointing at this folder.
4. Transfer the resulting game data to `ux0:data/asphalt6/` via FTP or USB using VitaShell.

### Final File Structure in `ux0:data/asphalt6/`

```text
ux0:data/asphalt6/
├── libasphalt6.so     <- Native library extracted from the APK's hidden copy.inject archive
├── data/              <- Game data (packed file###.dat chunks, loose .swf/.tga/.mp4, pack.info)
├── logs/              <- Incremental debug logs (asphalt6_NNN.log)
└── cg/ glsl/          <- Shader cache (created at runtime/build)
```

---

## 🛠️ Building from Source

This port does **not** keep a local copy of `porting_tools/` — all build, deploy, log,
LiveArea, and crash-dump workflows are handled by **psvita-port-toolkit**, a standalone tool
kept outside this repository.

### Build Prerequisites

- **VitaSDK**, fully compiled with softfp usage (`vitasdk-softfp/vdpm`).
- VitaSDK libraries: `vitaGL`, `vitashark`, `kubridge`, `pthread`.
- FFmpeg static libraries (`avformat`, `avcodec`, `swresample`, `avutil`, `mp3lame`) for the
  software intro-video decoder.
- CMake and Make.

### Build Steps

```bash
cmake -Bbuild .
cmake --build build
```

This produces `build/asphalt6.vpk`. For day-to-day development (build + deploy + crash-dump
parsing), use **psvita-port-toolkit** instead of raw `cmake`/`make`.

---

## 🏗️ Project Structure

- `source/`: Native C/C++ loader — lifecycle (`main.c`, `java.c`, `dynlib.c`), the engine's
  runtime ARM patch set (`patch.c`, one confirmed bug/hook at a time), and video/input
  (`video.cpp`, `utils/touch.c`).
- `source/reimpl/`: Minimal reimplementations of the Android surfaces the engine expects (EGL,
  asset manager, memory, pthreads, errno, 64-bit time).
- `source/utils/`: On-device diagnostics — `watchdog.c` (thread/frame-pacing heartbeat),
  `breadcrumb.c` (ring buffer of intercepted calls), `logger.c` (unified, spam-collapsing log
  sink for the game's own logs plus vitaGL/FalsoJNI).
- `lib/`: Auxiliary libraries (`so_util`, `falso_jni`, `vitaGL`, `libc_bridge`, `fios`,
  `kubridge`, `sha1`).
- `extras/`: LiveArea assets (`icon0.png`, `bg0.png`, `pic0.png`, `startup.png`,
  `template.xml`).
- `PORTING_PLAN.md`: Living plan — engine findings, JNI export table, checklist.
- `port_progress.md`: Bug-by-bug diagnosis log, one confirmed bug at a time, backed by real
  console logs and crash dumps.
- [`README VITAGL.md`](README%20VITAGL.md): Reference for vitaGL's own build flags (including
  the speedhacks this port enables/avoids and why).

---

## ⚖️ Disclaimer

**Asphalt 6: Adrenaline** is a registered trademark of Gameloft. The work presented in this
repository is not "official" or produced or sanctioned by Gameloft or any other registered
trademark mentioned in this repository.

This software does not contain the original code, executables, assets, or other
non-redistributable parts of the original game product. The authors of this work do not
promote or condone piracy in any way. To launch and play the game on their PS Vita device,
users must possess their own legally obtained copy of the game in the form of an `.apk` file.

---

## 👥 Credits and Acknowledgements

- **Gameloft**: Original developers of Asphalt 6: Adrenaline and the "Glitch" engine.
- **TheFloW**: For `so_util`, `kubridge`, `FdFix`, and foundational techniques for loading
  Android executables on PS Vita.
- **Rinnegatamante**: For `vitaGL` and continued support to the PS Vita porting scene.
- **v-atamanenko**: For `FalsoJNI` and the `soloader-boilerplate` base template.
- **FFmpeg**: For the software video decoder used to play the intro FMV.
- **Vita Community**: To all developers and enthusiasts in the PS Vita homebrew community.

---

## License

This software may be modified and distributed under the terms of the MIT license.
See the [LICENSE](LICENSE) file for details.
