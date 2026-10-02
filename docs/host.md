# The host

`build/ra2.exe` is a 32-bit Windows program on pcrecomp's `runtime/native32`.
`src/runtime/host.c` is everything specific to this game; the rest is the
toolkit's.

## Why native32

Yuri's Revenge is a Win32 game that talks to DirectDraw, DirectSound,
Winsock and COM. native32 maps `gamemd.exe` at its own base (0x00400000) in a
32-bit host, so a pointer the guest holds is a pointer Windows can use, and
every import goes to the real DLL through one bridge that measures the stack
purge. No per-API shims are needed to run; the ones in `host.c` exist to
change behaviour (headless, registration-free COM), not to make calls work.
Gunman Chronicles and The Movies run the same way.

The host links at `/BASE:0x60000000` so the guest's range is free.

## What host.c changes

| Import | Always | `--headless` | Why |
|---|---|---|---|
| `GetModuleHandleA(NULL)` | x | | the guest's hInstance is its own image, not the host |
| `GetModuleFileNameA` | x | | the game finds its files next to `gamemd.exe` |
| `GetCommandLineA` | x | | the guest's own command line |
| `CoCreateInstance` | x | | serves `Blowfish.dll` without registry entries (bringup.md, 2) |
| `MessageBoxA` | | x | printed, answered No/OK |
| `CreateWindowExA`, `ShowWindow` | | x | the window exists but is never shown |
| `DirectDrawCreate` | | x | the IDirectDraw vtable is patched: see below |
| `GetSystemMetrics` (screen size) | | x | the mode's size once one is set, as after a real mode change |
| `ClientToScreen`, `ScreenToClient` | | x | identity: the window is the whole screen (bringup.md, 5) |
| `_BinkSetSoundSystem@8` | | x | refused, movies play silent: a stopgap (bringup.md, 6) |

Before binding imports the host loads the system `ddraw.dll` by full path and
then adds the game folder to the DLL search path. The folder carries
`binkw32.dll`, which the game imports, and also DDrawCompat as `ddraw.dll`,
which is a fullscreen shim of its own and is not wanted.

## Headless DirectDraw

The game asks for exclusive fullscreen at 800x600x16 and draws by blitting
into the primary surface. Over RDP that mode change would resize the phone's
session. So headless keeps the real DirectDraw object and patches four
methods in its vtable:

- `SetCooperativeLevel` always gets `DDSCL_NORMAL`;
- `SetDisplayMode` is remembered and not applied, and `GetDisplayMode`
  answers the remembered mode;
- `CreateSurface` turns the primary into an offscreen system-memory surface
  of that mode, and gives every surface without its own pixel format the
  mode's RGB565 (with `DDSCAPS_OFFSCREENPLAIN`, which a formatted surface
  needs).

After `SetDisplayMode` the hidden window is resized to the mode, as the
fullscreen window would be.

The game cannot tell. A blit into the primary is a frame, and `--record
out.mp4` reads the primary at 30 fps from a host thread and pipes it to
ffmpeg. It copies the frame out under the lock and converts afterwards: holding
the lock across the pipe write starved the game's own `Lock`, and the movie
froze. Every 300 frames it logs a checksum, which is how the conformance
harness tells that the picture is moving.

This is also the seam the remaster's presentation layer goes in: the game
already draws into a surface the host owns.
