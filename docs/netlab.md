# On recomp-netlab

[recomp-netlab](https://github.com/sp00nznet/recomp-netlab) builds this repo
on Linux builders (clang-cl + xwin, x86), runs it on the lab's Windows and
Linux machines, plays scripted scenarios across them, and keeps A/B builds in
slots. Its recipe is `projects/redalert2-recomp.env` there; this page is the
RA2 side of it.

## Building on the farm

```sh
./netlab build redalert2-recomp            # -> ra2\build-farm\ra2.exe, about 4.5 minutes cold
./netlab build redalert2-recomp --slot b   # an A/B variant: build-farm-b\
```

The lifted C (`src/recomp/gen`) is generated here by `run_lift.py` and goes
to the builder with the checkout (280 MB the first time, changed files after);
`game\` and `work\` never leave this machine. The C runtime is linked in
(`/MT`), so the exe needs no Visual C++ redistributable on a test machine.

The first clang-cl build ran to its first import and jumped to 0: native32's
bridge to Windows read its locals through `esi` after moving it (pcrecomp
#47). With that, the clang-cl build plays the whole suite.

## A/B: the suite against any build

```
set RA2_EXE=G:\recomp\pc\ra2\build-farm\ra2.exe
py -3 tools\playtest.py                     # the suite on the farm's clang-cl build
py -3 tools\conformance.py                  # likewise
```

`RA2_EXE` points the playtest runner and the conformance harness at another
build of the host; without it they use `build\ra2.exe` (MSVC).

## RA2 against RA2 over the LAN

```sh
scenarios/redalert2/lan.sh local testbox   # in the recomp-netlab checkout
```

This PC hosts, the test VM joins: Network, New (the host's game screen,
`0xBC`); on the other side Network, the host's game in the list, Join and
Accept (`0xBD`); then the host's Start Game, and both are checked in the
game (`Capture_Mouse`) and pictured. Each side plays a script in the game's
own input, `tools/lan/host.args` and `joiner.args`, passed with `--args`
(netlab's role settings can't hold spaces), in the presenter window
(`--run`, `--mute`).

- **Pictures come from the game**, not the screen: the scripts write the
  game's frame every 10 s (`--hd-voxels-dump lan-frames`) and the scenario
  fetches the newest. A screen capture of the window picks up whatever is
  over it, which on a machine someone uses can be their own programs.
- **Both machines on one subnet.** IPXEmu (the game folder's `wsock32.dll`,
  loaded by the host from there) finds games by UDP broadcast, so the test VM
  comes onto the LAN for this (`nat/vm-to-lan.sh`, and back with
  `nat/vm-to-nat.sh`), with its machine file pointing at the LAN address.
- **The test VM needs the game** in `RA2_GAME` (its machine file), with its
  own player name (`RA2MD.INI` `[MultiPlayer] Handle`, hex) so the two
  differ.
- The host's game is item 1 of the lobby's games list; item 0 is the lobby.
- After the start the scripts move the cursor onto the battlefield: left on
  a menu button it sits past the 640x480 edge, and edge-scrolling runs the
  view into the map's black margin.

What the first run needed from netlab itself: a first run of a project on a
remote machine stopped before it started (`schtasks /End` of a task that did
not exist yet ended `netlab run` under `set -e`), and `RUN_FILES` names files,
not a folder.
