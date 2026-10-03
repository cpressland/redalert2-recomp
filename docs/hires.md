# High resolution and widescreen

Yuri's Revenge takes its in-game resolution from `RA2MD.INI`:

```ini
[Video]
ScreenWidth=1920
ScreenHeight=1080
```

The menus stay at 800x600 (the shell resolution); a game, a skirmish or a
mission, opens at the INI's size. On the virtual display any size is accepted
(`SetDisplayMode` is remembered, not applied), and the presenter shows the
picture at its own aspect: a 16:9 game fills a 16:9 window or screen, the 4:3
menus are letterboxed in it.

## What works

The playtest suite checks each size in a skirmish, and 1080p in a campaign
(`py -3 tools/playtest.py 'res-*'`):

| Resolution | Skirmish | Campaign | Notes |
|---|---|---|---|
| 1280x720 | pass | | |
| 1920x1080 | pass | pass (Allied, first mission) | sidebar of 16 cameo slots; view 1752x1048 |
| 2560x1440 | pass | | |
| 3840x2160 | pass | | sidebar rows capped at 30 (below) |

A larger resolution shows more of the battlefield, not a bigger picture: the
art stays at its native size, and the presenter's sharp scaling is what makes
it large on a 4K screen.

## The 4K limit is RA2's

At 3840x2160 the game faults just after allocating its view buffers:

```
[game] SidebarSurface (168x2160) SYSTEM MEMORY
[game] Set_View_Dimensions(0,0,3672,2128)
[game] Allocating ZBuffer (3672x2128)
=== fault 0xC0000005 at ... read of 0x00000064 in lifted sub_006ABD30
```

The shipping code does the same under `--original`
(`[original] exception 0xC0000005 at 006ABF96 (read 0x00000064)`), so it is not
the lift. `0x006ABF6C` lays out the sidebar's cameo buttons: rows =
`(height - 26 - top) / 50`, two buttons per row, written into a static array
at `0x00B07E80` with 0x38-byte entries. The next object starts at
`0x00B0B328`, which leaves about 240 entries, and at 2160 lines the loop runs
past the end into whatever follows (a null vtable).

## The fix: cap the rows

The lift caps the row count at 30 (`run_lift.py`, `sidebar_rows_patches`):
60 buttons, a tab's share of the array. Every place that computes the rows
divides by 50 the same way (`imul 0x51EB851F`, `sar 4`, `add r, r >> 31`), so
the patch finds each by that shape in the sidebar code that reads its height
(`0x00886F9C`) or top (`0x00B0B4F8`) and adds one line of C after the `add`:

```c
if ((int32_t)eax > 30) eax = 30; /* remaster: sidebar rows, run_lift.py sidebar_rows_patches */
```

It finds 26 sites. Up to 1440 lines the rows are 27 or fewer and nothing
changes; at 2160 the strip is 30 rows and the space below it stays empty.
`--original` runs the shipping code unpatched and still crashes at 4K, which
is the point: the oracle is the original game, the remaster is the lift.
