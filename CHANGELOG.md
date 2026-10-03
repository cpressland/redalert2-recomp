# Changelog

All notable changes to this project. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [SemVer](https://semver.org/).

## [Unreleased]

### Added
- HD voxels for shadows and aircraft: a unit's shadow at 2x (solid, with a
  2x edge, where RA2's own is a 1x stipple), and voxels drawn straight onto
  the battlefield (aircraft, buildings' voxel parts). 2x images are
  remembered by their 1x pixels, so the extra passes run only for new
  images. `RA2_FRAME_STATS=1` prints the time between frames.
- HD vehicles: voxel units drawn at twice the resolution of the picture.
  The render's last stage runs three more times at half-pixel offsets and
  the four images interleave into one at 2x; the host follows each unit's
  image through the game's own blits and the presenter shows the 2x pixels
  wherever the finished frame still shows the unit (docs/voxels.md). In the
  settings menu, on by default; `--hd-voxels` for headless runs, and
  `--hd-voxels-dump DIR` for 1x/2x renders and 2x frames.
- The voxel renderer, mapped (docs/voxels.md): the draw path from
  TechnoClass's vtable to the rasterizers, the 256x256 colour and depth
  buffers, the shade table and the cache, as groundwork for HD voxels.
- `RA2_HOST_ARGS`: extra host flags for every playtest case.
- The presenter, now the default display: the game in its own Direct3D 11
  window with sharp-bilinear, smooth, CRT, nearest and integer scaling (F12),
  borderless fullscreen (F11, Alt+Enter), native resolution on high-DPI
  screens, and input mapped through the scaling. `--classic` keeps the
  original exclusive-fullscreen DirectDraw.
- A virtual screen: every screen coordinate the game sees is relative to its
  main window, so the game's windows can sit anywhere (offstage moves them).
- High resolution and widescreen: 1280x720, 1920x1080 and 2560x1440 tested
  and 3840x2160 tested in game (`res-*` playtest cases, which set
  `RA2MD.INI` per case). 4K faulted in RA2's own sidebar layout, on the
  shipping code too; the lift caps the sidebar's rows at 30 (docs/hires.md).
- Remaster patches in `run_lift.py`: a line of C after a named instruction,
  for fixes the original cannot have; `--original` stays unpatched.
- The presenter's settings menu (F10, or right-click beside the picture):
  scaling, bars, fullscreen, and the game's resolution, without editing the
  INI. Blurred bars beside the 4:3 menus. Settings and the window's place are
  kept in `build
a2.ini`.
- `tools/present_drive.ps1`: clicks the presenter window like a player, for
  testing it.
- The pipeline for Yuri's Revenge `gamemd.exe` 1.001: RTTI (954 classes), the
  function catalog (24,940 functions), and the lift driver `run_lift.py`
  (24,954 functions, 0 lift errors).
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

- `tools/playtest.py`, the playtest suite (civ3's framework adapted): every
  menu screen and way back, a skirmish from setup to the score screen, both
  campaigns into their first mission, in parallel with per-case game folders,
  logs, recordings and contact sheets. 23 of 23 cases passing.
- Scripted input for headless runs: `--press DLG:CTRL@s` presses a menu button
  by dialog and control ID once that dialog is open, `--select`, `--waitlog`,
  `--key`, `--move`, `--click`, `--wait`. `tools/dialogs.py` maps all 98 menu
  dialogs to their controls.
- `--original`: the shipping machine code under the same host, shims and
  script, to tell lift bugs from host bugs.

### Fixed
- Every vehicle was invisible: the voxel rasterizers' table entries had no
  function, because MASM's alignment fillers before them read as a hot-patch
  prologue (pcrecomp #44). The playtest suite now fails a run on an
  unresolved `ICALL`, which is how 28/28 passed without them
  (docs/bringup.md 12).
- A race between the recorder writing a frame and the watchdog closing the
  recording ended some runs with 0xC0000409; the pipe is now locked.
- Exit Game hung on a spin-wait for the sound thread (pcrecomp #42), and the
  LAN game setup screen crashed on a mis-lifted `push; jmp` (pcrecomp #43).
- The recorder faulted at the mode switch and at exit, locking surfaces the
  game had released; it now holds its own reference.
- In-game recordings were sheared: frames are scaled to the recording's size.
- Two window procedures were missing from the catalog (seeded).
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
