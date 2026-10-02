# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

### Known issues
- Bink's pacing stalls partway through the intro (bringup.md, 6). Headless
  refuses Bink's sound system meanwhile, so movies play silent.

### Added
- The pipeline for Yuri's Revenge `gamemd.exe` 1.001: RTTI (954 classes), the
  function catalog (22,682 functions), and the lift driver `run_lift.py`
  (23,458 functions, 0 lift errors).
- `run_lift.py --seeds`: entries a run found that the catalog lacks.
- The host, `build/ra2.exe`, on pcrecomp `native32`. Boots through the CRT,
  WinMain, COM registration and DirectDraw setup.
- Headless mode: hidden window, DirectDraw kept out of fullscreen with the
  mode remembered and not applied, `--record out.mp4 --frames N` from the
  primary surface.
- Registration-free COM: `Blowfish.dll` is served from the game folder when it
  is not registered, so no admin step is needed.
- Conformance harness, `tools/conformance.py`: 7/7 boot milestones, up to
  the intro movie playing.
- The Westwood logo and the Yuri's Revenge intro play headless, drawn by Bink
  into the game's primary surface; screenshots in the README.

### Fixed
- Headless: the intro movie was centred on the real desktop and Bink wrote
  past the primary. After `SetDisplayMode` the hidden window takes the mode's
  size, `GetSystemMetrics` reports it, and `ClientToScreen` is identity.
- `--record` held the primary's lock across the pipe write and starved the
  game's own `Lock`; it now copies the frame out first.
- 66 functions missing from the catalog, among them a static constructor,
  because their catalog entry started in the alignment padding before them.
  Fixed in pcrecomp #32.
