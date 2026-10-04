# Roadmap

## Wrapping up Red Alert 2

In order; each is done when its check is in the suite or the docs.

Done: the toolkit fixes found here are in pcrecomp `main` (#41 to #44), and
Setup.cmd has been run end to end from a clean folder (the ZIP download).

1. **Play, not just reach.** Done: `skirmish-build` deploys, builds a power
   plant and a barracks and trains a GI, checked in the game's event log.
   Next: a mission played to its win.
   A fight that throws voxel debris under the camera comes with it, and HD
   voxel animations go on by default once it passes.
2. **Red Alert 2 itself** (`game.exe`, same install): catalog, lift, the same
   host and patches re-found by their shapes; the suite's cases for its menus.
3. **The rest of the menus** in the suite: the remaining Options screens,
   Load with a save present, WOnline as far as it goes
   without servers.
4. **Audio in `--record`**, and a native reference run (the shipping
   `gamemd.exe` recorded under offstage) to compare frames against.
5. **Multiplayer**: the LAN game works between two PCs (docs/testing.md).
   Next: a match played to an end, more than two players, and the same
   across a NAT.
6. **Release**: v0.1.0.

## Tiberian Sun and Firestorm

Its own repo, `tiberiansun-recomp`, the way civ, civ2 and civ3 are three:
each game's catalog, lift, patches and hooks are tied to its exe's addresses.
It started from this repo's host, presenter, scripted input and test tools;
moving that shared host into pcrecomp, so both games use one copy, is still
to do.

## The remaster

The reason for the project. Each item sits on code the recompilation owns,
not on patches to a binary:

- **Presentation layer.** Done: the presenter, its settings menu, blurred
  bars beside the 4:3 menus, remembered settings (docs/presenter.md), and
  720p to 4K in game (docs/hires.md).
- **HD voxels.** Done for units, their shadows and aircraft
  (docs/voxels.md). Voxel animations and debris are written and opt-in: next
  is a test that puts one on screen (an explosion with debris under the
  camera). Then the units drawn by 0x0073C5F0, and 2x above a game
  resolution of 2048x1080.
- **Higher-resolution art paths**, where a larger source exists or can be
  produced, behind the same asset loaders.
- **Modern input and audio**: raw mouse, rebindable keys, DirectSound replaced
  by a modern backend.

## Deferred

- Online play (Westwood Online / CnCNet): the servers are gone; LAN over IPXEmu
  stays as shipped.

## Out of scope

- Distributing the game, its data, or the lifted C.
- Reimplementing the game: that is what OpenRA does, and does well.
