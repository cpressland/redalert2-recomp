# Bring-up log

Each wall the recompiled `gamemd.exe` hit on the way up, with the evidence and
the fix. Newest last. Commands are run from the repo root over RDP, so every
run is `--headless` (REPO_RULES section 13).

## 1. Thirteen static constructors were never lifted

```
  entering 0x007CD80F
ICALL: unresolved VA 0x0040FF90 from 0x007CBED3
ICALL: unresolved VA 0x00410010 from 0x007CBED3
...                                              (13 in all)
[messagebox] type 0x30 -> 1: ***FATAL*** String Manager failed to initilaized properly
```

`0x007CBED3` is the CRT's `_initterm`, walking the C++ constructor tables
(`__xc_a`..`__xc_z` and the C initialisers). The tables hold 3,952 function
pointers; 13 of them were not in the catalog. An unresolved `RECOMP_ICALL`
answers `eax = 0` and carries on, so the run went on without those globals.

Fix: `run_lift.py --seeds` (default `work/run_seeds.json`) injects addresses
the catalog lacks. All 3,952 table entries were read statically and the 13
missing ones seeded at once. Root cause of most of them: see 4.

## 2. The "String Manager" box is really a COM check

The message box above survived the constructors. The string fetch that
produced it is `0x00734E60`: it returns that fatal text whenever the CSF
string table is not loaded yet, so any error before the language MIX loads
shows up as "String Manager failed". The real error was a few lines earlier
in WinMain (`0x006BD6CA`): a flag set when the game cannot create its COM
servers.

`Blowfish.dll`, the Westwood Online cipher, ships in the game folder as an
in-process COM server. WinMain `CoCreateInstance`s it, falls back to
`LoadLibrary` + `DllRegisterServer`, and retries. The Steam install script
registers it under `HKCR` with admin rights; on this machine it never had, and
an unelevated `DllRegisterServer` returns `S_OK` while writing nothing.

Fix (host): `CoCreateInstance` answering `REGDB_E_CLASSNOTREG` is retried
against the game folder's own servers through `DllGetClassObject`, the way a
side-by-side manifest would. Nothing is written to the registry and no admin
step is needed.

```
[com] class 1440AD10 not registered: served by Blowfish.dll -> 0x00000000
[headless] CreateWindowExA("Yuri's Revenge", 1804x972) from sub_00777C30 -> hidden hwnd 00CC0B76
[headless] DirectDrawCreate -> 0x00000000
[headless] SetCooperativeLevel(0x11) -> NORMAL: 0x00000000
[headless] SetDisplayMode(800x600x16) -> kept, not applied
```

Not needed: the launcher check. The Steam build already returns true from
`0x0049F5C0` (`mov al, 1; ret`), so `gamemd.exe` runs without `RA2MD.exe`.

## 3. Surfaces with a format need a type

```
[headless] CreateSurface(flags 0x7 caps 0x0 64x64) -> 0x88760091
=== fault 0xC0000005 ... in lifted sub_004BAD80
```

Headless DirectDraw gives every surface without a pixel format the game's
16-bit RGB565 (a 32-bit desktop would hand out 32-bit ones). A surface with
caps 0 and an explicit format is `DDERR_INVALIDPIXELFORMAT`; it has to say
`DDSCAPS_OFFSCREENPLAIN`. The game does not check the result, hence the fault.

## 4. Catalog entries in alignment padding hid 66 real functions

```
ICALL: unresolved VA 0x0040A090 from 0x00402C70
=== fault 0xC0000005 ... write of 0x00000094 (null/low) in lifted sub_00407860
```

`0x0040A090` is named by a data table (`0x00816374`) and is a perfectly
ordinary method. The catalog had an entry at `0x0040A08E` instead: two bytes
into the `nop` run before it, put there by a `call rel32` decoded out of the
middle of another instruction. Decoded as a function, that entry walked the
padding into the real body and owned its instructions, so when the data scan
reached `0x0040A090` it was already "covered" and skipped.

66 catalog entries were like that, each hiding the 16-byte-aligned function
behind it; `0x0045B110`, one of the 13 constructors in 1, is another.

Fix (toolkit): disasm32 moves a candidate whose bytes up to the next 16-byte
boundary are all `nop`/`int3` to that boundary
(pcrecomp `fix/disasm32-padding-entries`).

## 5. The intro video is centred on the real desktop

```
[headless] CreateSurface(flags 0x1 caps 0x200 800x600) primary -> 0x00000000
[headless] frame 1 blitted to the primary
=== fault 0xC0000005 at 0x10016B7D
  write of 0x0EF2B000
  in lifted sub_00432F36, last native call binkw32.dll!_BinkCopyToBuffer@28
```

The first frame is in, then Bink writes past the end of the primary. Its
arguments, logged from a shim: `copy 800x600 -> pitch 1600 h 600 at 560,240`.
560,240 is an 800x600 movie centred on 1920x1080, the real desktop. In
fullscreen the mode change would have made the screen 800x600 first; headless
it never happened. The movie player also adds the window's client origin
(`ClientToScreen`, `0x00432EF0`), which in fullscreen is 0,0.

Fix (host, headless): after `SetDisplayMode` the hidden window is resized to
the mode, `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)` answers the mode, and
`ClientToScreen`/`ScreenToClient` are identity. That is the world the game was
written for: one window that is the whole screen.

## 6. The intro plays, and Bink's pacing stalls

With 5 fixed the recording still showed one frame for 90 s. Not the game: the
recorder held the primary's lock while writing 1.9 MB to the ffmpeg pipe, and
the game's own `Lock` of the primary waited on it. The recorder now copies the
frame out under the lock and converts after; frame checksums go to the log
every 300 frames.

```
[record] frame 600 at 0FE98028 checksum 04EE4C3E
[record] frame 900 at 0FE98028 checksum E62E9C39
[record] frame 1200 at 0FE98028 checksum EA66FFAB
[record] frame 1500 at 0FE98028 checksum EA66FFAB      <- frozen from here
```

The Westwood logo and the Yuri's Revenge intro (800x600, 3,579 frames at
15 fps, about 4 minutes) play, drawn by Bink into the primary. Then the picture
freezes and stays frozen: the game spins in `BinkWait` (59.6M calls in 150 s),
and Bink never says the next frame is due.

| Bink sound | Runs | Froze at |
|---|---|---|
| on (DirectSound) | 2 | ~40 s, both |
| off (`BinkSetSoundSystem` refused) | 2 | ~110 s; not within 150 s |

So sound makes it reliable but does not cause it. The machine was also running
other projects' compiles at the time. Headless now refuses Bink's sound system,
which is a stopgap with a known ceiling (`ponytail:` in `host.c`); the root
cause is open (ROADMAP).
