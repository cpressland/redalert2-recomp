# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

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
- The main menu: the recompiled game reaches it after the full intro and draws
  it completely. Conformance 8/8.
- `--debuglog`: the game's own debug log, through `run_lift.py` `HOOKS` (a
  lifted function given a host body).

### Fixed
- Movies froze for good at a timing-dependent moment: the game pauses Bink
  while its window is not active, and a hidden window never is. Headless
  delivers every activation message as "active".
- The main menu returned at once: its dialog resource was looked up in the
  host, because a NULL module means the process exe. Resource and dialog calls
  now map NULL to the guest image.
- The menu drew no buttons: hidden windows get no `WM_PAINT`. Headless windows
  are now layered at alpha 0 instead of hidden.
- The insert-disc box: a lift bug dropped init's strcat after `call sprintf`,
  so the CD search path was empty. Fixed in pcrecomp #41.
- Headless: the intro movie was centred on the real desktop and Bink wrote
  past the primary. After `SetDisplayMode` the hidden window takes the mode's
  size, `GetSystemMetrics` reports it, and `ClientToScreen` is identity.
- `--record` held the primary's lock across the pipe write and starved the
  game's own `Lock`; it now copies the frame out first.
- 66 functions missing from the catalog, among them a static constructor,
  because their catalog entry started in the alignment padding before them.
  Fixed in pcrecomp #32.
