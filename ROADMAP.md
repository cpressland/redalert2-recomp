# Roadmap

## Next

1. **Play, not just reach.** In-game input for the suite: select and deploy
   the MCV, build a power plant and a barracks, train a unit, and check them in
   the game's own log. Then a mission played to its win.
2. **Every menu screen in the suite**: the remaining Options screens, Load
   with a save present, the LAN host screen's Start, the WOnline screens as far
   as they go without servers.
3. **Record the audio** with `--record`.
4. **Native reference run.** Record the original `gamemd.exe` under offstage at
   the console for the same seconds of startup, as ground truth to compare
   frames against.
5. **Upstream the toolkit fixes**: pcrecomp #41, #42, #43, #44.
6. **Setup.cmd** end to end from a clean folder, then the shortcut.

## The remaster

The reason for the project. Each item sits on code the recompilation owns,
not on patches to a binary:

- **Presentation layer.** Done: the presenter, its settings menu, blurred
  bars beside the 4:3 menus, remembered settings (docs/presenter.md), and
  720p to 4K in game (docs/hires.md).
- **HD voxels.** Done for units (docs/voxels.md). Next: their shadows,
  aircraft (copied onto the battlefield at 0x0073CDE9), voxel animations and
  debris, buildings' voxel parts, and 2x above a game resolution of 2048x1080.
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
