# Roadmap

## Next

1. **Bink's pacing stall** (bringup.md, 6): find why `BinkWait` stops
   releasing frames, then give movies their sound back and record the audio.
2. **Past the intro to the main menu**, then scripted input
   (`--key vk@s`, `--click x,y@s`, The Movies' shape) to skip movies and play
   into a skirmish headless.
3. **Native reference run.** Record the original `gamemd.exe` under offstage at
   the console for the same seconds of startup, as ground truth to compare
   frames against.
4. **Upstream the disasm32 padding fix** (pcrecomp `fix/disasm32-padding-entries`)
   and re-check the titles that use disasm32 for regressions.
5. **Setup.cmd** end to end from a clean folder, then the shortcut.

## The remaster

The reason for the project. Each item sits on code the recompilation owns,
not on patches to a binary:

- **Presentation layer.** The game already draws into a primary surface the
  host creates (host.md). Replace the DirectDraw blit-to-screen with a D3D11
  or Vulkan present: integer and smooth scaling, borderless window, vsync,
  high-DPI, no DDrawCompat.
- **Resolution.** The engine takes `ScreenWidth`/`ScreenHeight` from
  `RA2MD.INI`; the sidebar and UI layout are what break at large sizes. Fix the
  layout in the lifted code so 1080p and 1440p play properly.
- **Higher-resolution art paths**, where a larger source exists or can be
  produced, behind the same asset loaders.
- **Modern input and audio**: raw mouse, rebindable keys, DirectSound replaced
  by a modern backend.

## Deferred

- `game.exe` (Red Alert 2 without the expansion): same engine, same pipeline,
  once `gamemd.exe` runs.
- **Tiberian Sun / Firestorm** (`Game.exe`, 2000): same engine lineage, 744
  of its 807 classes share names with Yuri's Revenge (RECON.md). Its own repo,
  after this one runs.
- Online play (Westwood Online / CnCNet): the servers are gone; LAN over IPXEmu
  stays as shipped.

## Out of scope

- Distributing the game, its data, or the lifted C.
- Reimplementing the game: that is what OpenRA does, and does well.
