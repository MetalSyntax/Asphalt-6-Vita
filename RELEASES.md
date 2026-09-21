# Release Notes

Versioned snapshots of this port's status. Every claim here is backed by a real console
log/crash-dump in [`port_progress.md`](port_progress.md) — nothing in this file is
speculative. See the [README](README.md#-known-issues) for the always-current summary of
known issues; this file is the dated, per-release history.

---

## v0.1.0-alpha — 2026-09-20

**Status: Alpha, testing only. Relatively playable, but the overall experience is far from
acceptable.** This build boots, reaches the main menu, and lets you finish a race — but
expect frequent graphical glitches, aggressive frame-rate drops, and largely broken audio.
Use this release to help test/debug the port, not to actually play the game.

### What works

- Native ARM execution of `libasphalt6.so` on-device (no emulation/interpretation).
- Boots through the full JNI lifecycle to the Flash-based main menu (`gameswf`,
  `GS_MenuMain`), presenting frames continuously.
- Races start and can be completed: touch controls and physical controls (D-Pad/stick to
  steer, SQUARE to brake, CROSS for nitro) both work.
- Intro FMV plays via a software FFmpeg decoder.
- Engine sound (car audio) is audible during races.

### Known issues (confirmed on real hardware)

- **Audio — only the engine/motor sound plays.** Menu music and sound effects
  (`GLMediaPlayer`, crash/nitro/UI sounds) are implemented in code (samples decoded from
  `file00a.bin`, mixed in `source/reimpl/gmp_audio.c`) but do not come through on console —
  the only audio you'll hear is the car engine. Not yet root-caused against a real audio
  log.
- **Graphical errors while textures load.** Visible corruption/glitches tied to texture
  streaming during gameplay.
- **Aggressive, frequent FPS drops during races.** Frame pacing swings from a low-30s
  ceiling down to single digits mid-race. A likely contributor (an over-broad scene-culling
  bypass forcing the engine to draw all static scenery regardless of camera frustum) has
  been split into its own build flag (`RENDER_SCENE_CULLING_BYPASS`, off by default in this
  release) but the fix is **not yet confirmed on hardware**.
- **World geometry pop-in.** Houses, trees, and road signs used to render only at a very
  short distance from the car, popping in right next to it instead of loading at range. A
  fix is included in this release (forces the engine to use each track's own LOD distance
  instead of a hardcoded "generic phone" value) but is **unverified on hardware**.
- **In-race pause menu is broken.** The pause menu's buttons never update visibility
  correctly (the engine looks them up by name and doesn't find them in this build's
  HUD/profile variant). A crash-avoidance guard is in place, but the menu itself is still
  non-functional.
- **Elements intermittently disappear during races, including the player's car.** Traced to
  real, CPU-bound multi-second stalls inside the engine's mesh-batching code; root cause
  narrowed down, dedicated fix still pending.
- **Graphical glitch on vehicle windows**, caused by a car-reflection texture missing from
  the original APK data (a missing-asset issue, not a code bug).

### Fixed in this release

- Crash when exiting to the main menu and tapping near the (missing) Info screen — the
  engine dereferenced a null smart pointer every frame while hit-testing cursor/touch input
  (Bug #040).
- Physical CROSS button: repositioned the synthetic nitro touch so it no longer overlaps the
  "steer right" touch zone (it was accidentally steering the car instead of firing the
  nitro).
- World geometry pop-in root cause identified and patched (see above, Bug #041).
- Split the scene-culling bypass flag to stop forcing every static scene object to render
  regardless of camera frustum, as a likely fix for the FPS ceiling during races (see above).
- Reduced per-frame logging overhead on two race-loop guards that were firing on nearly
  every frame.

---

## Versioning

This project doesn't follow strict SemVer yet — version numbers track port maturity
(`0.x.y-alpha` while core systems are still being stabilized). A `1.0.0` tag will only be
cut once the known-issues list above is empty and a full race, menu-to-menu, has been
verified glitch-free on real hardware.
