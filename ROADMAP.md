# Roadmap

## Wrapping up Red Alert 2

In order; each is done when its check is in the suite or the docs.

1. **Upstream the toolkit fixes**: pcrecomp #41, #42, #43, #44, so the repo
   builds from pcrecomp `main` and Setup.cmd needs no integration branch.
2. **Setup.cmd end to end** from a clean folder: copy the game, catalog,
   lift, build, shortcut. The README's Quick start, proven.
3. **Play, not just reach.** In-game orders in the suite (`--drag` and
   modifier clicks are in): deploy, build a power plant and a barracks, train
   a unit, checked in the game's own log; then a mission played to its win.
   A fight that throws voxel debris under the camera comes with it, and HD
   voxel animations go on by default once it passes.
4. **Red Alert 2 itself** (`game.exe`, same install): catalog, lift, the same
   host and patches re-found by their shapes; the suite's cases for its menus.
5. **The rest of the menus** in the suite: the remaining Options screens,
   Load with a save present, the LAN host's Start, WOnline as far as it goes
   without servers.
6. **Audio in `--record`**, and a native reference run (the shipping
   `gamemd.exe` recorded under offstage) to compare frames against.
7. **Release**: v0.1.0, private (section 1 of the house rules decides any
   public flip separately).

## Tiberian Sun and Firestorm (next repo)

Its own repo, `tiberiansun-recomp`, the way civ, civ2 and civ3 are three:
each game's catalog, lift, patches and hooks are tied to its exe's
addresses. What carries over without changes is the host, and that moves
into pcrecomp first rather than being copied:

1. **Upstream the generic host** into pcrecomp: headless DirectDraw (virtual
   display, invisible windows, focus faking), the D3D11 presenter, scripted
   input and the playtest runner. Red Alert 2 then uses it from pcrecomp.
2. **Recon** of `Game.exe` (Tiberian Sun with Firestorm, 3.8 MB):
   RTTI, the catalog with #44's MASM filler fix from the start, the lift.
   92% of its classes have a namesake here (docs/RECON.md), so this repo's
   docs (bring-up, voxels, hi-res) are its map.
3. **Bring-up** to the main menu with the shared host, then the suite's
   menu, skirmish and campaign cases.
4. **The remaster**: the presenter, resolutions (check its sidebar for the same
   kind of limit), HD voxels re-found by their shapes: the finish stage, the
   span records, the blits.

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
