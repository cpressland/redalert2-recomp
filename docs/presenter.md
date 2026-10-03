# The presenter

`build\ra2.exe --run` shows the game in its own Direct3D 11 window
(`src/runtime/present.c`). The game runs exactly as it does headless: its
display is virtual and its windows are invisible, and the presenter shows the
picture and carries input back. `--classic` is the game's own exclusive
fullscreen DirectDraw, as it shipped.

```
build\ra2.exe --run                               # a window at 85% of the screen's height
build\ra2.exe --run --fullscreen                  # borderless fullscreen
build\ra2.exe --run --scale crt                   # sharp | smooth | crt | nearest | integer
build\ra2.exe --run --classic                     # the original display
```

| Key | |
|---|---|
| F11, Alt+Enter | borderless fullscreen on the window's monitor, and back |
| F12 | scaling: sharp-bilinear, smooth, CRT, nearest, integer |
| F10, or right-click beside the picture | the settings menu |

## Settings

The settings menu has the scaling, the bars, fullscreen, and the game's
resolution. The presenter remembers its own choices, and the window's place
and size, in `builda2.ini`:

```ini
[present]
scale=sharp
bars=blur
fullscreen=0
window=100,100,1600,900
```

A `--scale` or `--fullscreen` on the command line wins over the file.

- **Bars.** The 800x600 menus in a 16:9 window or screen leave bars at the
  sides. `blur` (the default) fills them with a soft, darkened copy of the
  picture stretched over the whole window, so the menus sit in a frame rather
  than a hole; `black` is plain letterboxing. The shader draws the fill first
  (`mode 5`, a 5x5 box of wide taps) and the picture on top.
- **Game resolution** applies to the next game (skirmish or mission); the
  menus are always 800x600. It is written to the game's `RA2MD.INI`
  `[Video] ScreenWidth/ScreenHeight`, and to the game's options in memory
  (`0x00A8EB60` +0x24/+0x28, the object the settings reader is called on at
  `0x0052C630`), because the game saves its own options on exit and would put
  the old size back. 3840x2160 needs the sidebar fix in [hires.md](hires.md).
- While the menu is open the picture holds still (the menu runs its own
  message loop on the presenter's thread); the game itself keeps running.

## How it fits together

| Part | Where | What it does |
|---|---|---|
| Virtual display | `host.c` (headless DirectDraw) | The game's mode is remembered, not set; its primary is a system-memory surface the host owns |
| Invisible game windows | `host.c` | Layered at alpha 0: painted by Windows, shown nowhere |
| Virtual screen | `host.c` (`vorigin`) | Every screen coordinate the game sees is relative to its main window's client corner |
| Presenter window | `present.c` | Copies the primary each vsync (`host_frame`), uploads it, draws it letterboxed through a shader |
| Input | `present.c`, `input.c` | Maps the player's mouse into game coordinates, posts it to the game window under it, answers `GetCursorPos`/`GetKeyState` |

## Why it is built this way

- **The game is not touched.** Like SimCity 2000's frontend and Gunman's
  presenter, everything sits around the original code. The playtest suite and
  the `--original` oracle test the same display path a player uses.
- **Sharp-bilinear is the default** (Gunman's shader): whole texels stay crisp
  and only their edges blend, so an 800x600 game upscaled 2.9x to a 4K-class
  window neither blurs nor shimmers. Because of that, the window does not need
  to be a whole multiple: it opens at 85% of the work area's height.
- **The presenter's thread is per-monitor DPI aware; the game's is not.**
  Unaware, Windows stretched the window by the display scale (2x at 200%)
  after the shader, and the picture came out blurred. The game's windows stay
  unaware, so nothing about how the game sees its screen changes. The hit test
  that finds the game window under a click runs in their (unaware) context:
  from the aware thread their rectangles came back in physical pixels and no
  button contained the point.
- **The screen is virtual.** The game was written for a fullscreen window whose
  client area is the screen, and offsets its drawing by window positions. Its
  windows now sit wherever Windows (or offstage, which moves every window of a
  program to its monitor) puts them, so `ClientToScreen`, `ScreenToClient`,
  `GetWindowRect`, `WindowFromPoint`, `MoveWindow`, `SetWindowPos` and
  `SetCursorPos` are answered relative to the main window's client corner.
  Without it, the menu's button captions were drawn off the surface.
- **A click goes to the deepest visible game window under it.** The main
  menu's dialog is not a direct child of the main window, so asking the main
  window's children found nothing. Static controls pass clicks to what is
  under them, as Windows does.
- **Mouse buttons are what the presenter saw.** The game's controls read
  `VK_LBUTTON` as well as the messages, and a click has to agree whether it
  came from a mouse, a pen or a test driver.

## Testing it

`tools\present_drive.ps1` clicks the presenter window the way a player does,
at game coordinates converted through the same letterboxing:

```
powershell -File tools\present_drive.ps1 -Clicks "20:720,220 26:720,300 33:714,262"
```

That is Single Player, Skirmish and Start Game, and the log shows each click
reaching its button:

```
[present] click at game 720,220 -> Button 05E5215C (id 1667) at 76,21
[present] click at game 720,300 -> Button 033C2B06 (id 1401) at 76,17
[present] click at game 714,262 -> Button 01F52A42 (id 1559) at 70,21
[game] Capture_Mouse()
```
