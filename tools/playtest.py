#!/usr/bin/env python3
"""Scripted play tests: walk every menu and game mode headless, in parallel, and keep the evidence.

Civilization III's runner (civ3 tools/playtest.py) adapted to RA2. A case is
a list of host arguments; the difference is how buttons are named. RA2's
menus are dialog resources, so a case says press('SinglePlayer') and the
runner resolves the label to a dialog and control ID with tools/dialogs.py's
map, and the host presses it once that dialog is actually open. No pixel
coordinates and no fixed times for menus (docs/testing.md).

A run leaves, in work/tests/<case>/:

    run.log      everything the host printed
    run.mp4      the recording
    sheet.png    a contact sheet, one frame every --every seconds

and the summary line says how it ended: the exit code (4 = the watchdog, the
normal end of a timed run), any fault or not-lifted report, the dialogs that
opened, and the frames blitted in game. Reading the sheets is the actual test:
a pass says a case reached what it expected and ran clean, not that every
pixel was right.

    py -3 tools/playtest.py --list
    py -3 tools/playtest.py menu-skirmish
    py -3 tools/playtest.py 'menu-*' 'back-*' --jobs 4
    py -3 tools/playtest.py skirmish-start --original   # the shipping code, same script

--original runs the shipping machine code under the same host, shims and
script (src/runtime/oracle.c), into work/tests/<case>.original/: where a case
fails on the lift and passes there, the lift is wrong; where both fail, the
host is.
"""
import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, 'build', 'ra2.exe')
OUT = os.path.join(ROOT, 'work', 'tests')
DIALOGS = os.path.join(ROOT, 'work', 'dialogs.json')

# The menu screens, by the dialog resource the game builds them from
# (py -3 tools/dialogs.py --show 0xE2 lists one).
MAIN, SINGLE, SKIRMISH, CAMPAIGN, LOADGAME = 0xE2, 0x100, 0x102, 0x94, 0xB7
OPTIONS, LAN, MOVIES, KEYBOARD, CONFIRM, NEWLAN = 0xD5, 0xBB, 0x101, 0xA3, 0x120, 0xBC

_dialogs = None


def control(dlg, label):
    """Control ID of the button captioned GUI:<label> in dialog `dlg`."""
    global _dialogs
    if _dialogs is None:
        if not os.path.exists(DIALOGS):
            subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'dialogs.py')], check=True)
        _dialogs = json.load(open(DIALOGS))
    for c in _dialogs['0x%X' % dlg]['controls']:
        if c['text'] == 'GUI:' + label:
            return c['id']
    raise KeyError('no GUI:%s in dialog 0x%X' % (label, dlg))


# Event times are seconds after the main menu first opened; --press and
# --select wait for their dialog on top of that, so they only need to come in
# the right order. One second apart is plenty.
class Script:
    def __init__(self):
        self.args, self.t, self.expect = [], 1.0, []

    def press(self, dlg, label, opens=None):
        """`label` is a GUI: caption, or a control ID for an uncaptioned one."""
        cid = label if isinstance(label, int) else control(dlg, label)
        self.args += ['--press', '0x%X:%d@%g' % (dlg, cid, self.t)]
        self.t += 1
        if opens is not None:
            self.expect.append(opens)
        return self

    def select(self, dlg, ctrl, n):
        self.args += ['--select', '0x%X:%d=%d@%g' % (dlg, ctrl, n, self.t)]
        self.t += 1
        return self

    def waitlog(self, text):
        """Hold until the game's debug log prints `text` (Capture_Mouse: in game)."""
        self.args += ['--waitlog', '%s@%g' % (text, self.t)]
        self.t += 1
        return self

    def move(self, x, y, after=0):
        self.t += after
        self.args += ['--move', '%d,%d@%g' % (x, y, self.t)]
        self.t += 1
        return self

    def key(self, vk, after=0):
        self.t += after
        self.args += ['--key', '%s@%g' % (vk, self.t)]
        self.t += 1
        return self


def S():
    return Script()


# name: (script, seconds the run lasts, what it must show)
#   expect 'dialogs': every listed dialog opened
#   expect 'ingame':  the game started (its own Capture_Mouse() log line) and
#                     the picture kept changing (5+ distinct sampled frames)
#   expect 'alive':   no human player was defeated
#   expect 'exit':    the exit code
CASES = {}


def case(name, script, seconds=60, **expect):
    expect.setdefault('dialogs', script.expect)
    CASES[name] = (script.args, seconds, expect)


case('menu-idle', S(), 40, dialogs=[MAIN])

# Every main-menu button, and the way back from each screen it opens. Exit
# Game asks first (dialog 0x120, OK/Cancel), so an exit is two presses; a
# clean exit 0 proves the main menu came back and still works.
EXIT = lambda s: s.press(MAIN, 'ExitGame', CONFIRM).press(CONFIRM, 'OK')
for label, dlg in [('SinglePlayer', SINGLE), ('Options', OPTIONS), ('Network', LAN),
                   ('MoviesAndCredits', MOVIES), ('WWOnline', 0x10E)]:
    case('menu-' + label.lower(), S().press(MAIN, label, dlg))
case('menu-exit', EXIT(S()), 60, exit=0)
case('menu-exit-cancel', S().press(MAIN, 'ExitGame', CONFIRM).press(CONFIRM, 'Cancel', MAIN))

case('back-singleplayer', EXIT(S().press(MAIN, 'SinglePlayer', SINGLE).press(SINGLE, 'MainMenu')), 60, exit=0)
case('back-options', EXIT(S().press(MAIN, 'Options', OPTIONS).press(OPTIONS, 'MainMenu')), 60, exit=0)
case('back-movies', EXIT(S().press(MAIN, 'MoviesAndCredits', MOVIES).press(MOVIES, 'MainMenu')), 60, exit=0)
case('back-network', EXIT(S().press(MAIN, 'Network', LAN).press(LAN, 'MainMenu')), 60, exit=0)

# The LAN lobby's New Game: the host's game setup screen.
case('lan-new', S().press(MAIN, 'Network', LAN).press(LAN, 'New', NEWLAN))

# Single player's three doors.
SP = lambda: S().press(MAIN, 'SinglePlayer', SINGLE)
case('sp-skirmish', SP().press(SINGLE, 'Skirmish', SKIRMISH))
case('sp-campaign', SP().press(SINGLE, 'NewCampaign', CAMPAIGN))
case('sp-load', SP().press(SINGLE, 'LoadSavedGame', LOADGAME))

# Options' sub-screens and the movie/credit players.
case('options-keyboard', S().press(MAIN, 'Options', OPTIONS).press(OPTIONS, 'Keyboard', KEYBOARD))
case('movies-credits', S().press(MAIN, 'MoviesAndCredits', MOVIES).press(MOVIES, 'ViewCredits'), 90)
case('movies-sneakpeeks', S().press(MAIN, 'MoviesAndCredits', MOVIES).press(MOVIES, 'SneakPeeks'), 90)

# Into the game. A skirmish on the default map and settings; each campaign
# from the campaign screen's list (0 and 1: Allied and Soviet), at the
# default difficulty. In game the screen is blitted every frame, so the frame
# count says the game is running, and the sheet says what it shows.
# A skirmish played through by nobody: in game, H (centre on base -- headless,
# the view starts off the player's base and shows only black shroud; the
# original does the same, so it is the host's), then nothing. The idle player
# is beaten (at start 52,97 in about 4.5 minutes, on the shipping code too),
# the skirmish score screen opens, and Continue goes back to the main menu:
# the whole loop, start to menu.
INGAME = 'Capture_Mouse'
case('skirmish-start', SP().press(SINGLE, 'Skirmish', SKIRMISH).press(SKIRMISH, 'StartGame')
     .waitlog(INGAME).move(236, 240, after=2).key('0x48', after=10), 180, ingame=True)
case('skirmish-loop', SP().press(SINGLE, 'Skirmish', SKIRMISH).press(SKIRMISH, 'StartGame')
     .waitlog(INGAME).move(236, 240, after=2).key('0x48', after=10)
     .press(0x108, 'Continue', MAIN), 720, ingame=True)
# The campaign screen starts a campaign from its emblems: uncaptioned static
# controls, Allied on top (1770) and Soviet below (1772), with "Click the
# Allied icon to start Allied campaign" under each (tools/dialogs.py --show 0x94).
for side, emblem in [('allied', 1770), ('soviet', 1772)]:
    case('campaign-' + side, SP().press(SINGLE, 'NewCampaign', CAMPAIGN)
         .press(CAMPAIGN, emblem).waitlog(INGAME).move(236, 240, after=2).key('0x48', after=10),
         300, ingame=True, alive=True)


def farm(d):
    """A private game folder for one case, made of hard links to game/.

    The game writes into its folder (RA2MD.INI, saves, debug files), so cases
    sharing one would clobber each other; links cost no space. RA2MD.INI is
    copied, with the intro off: four minutes of cinematic before every case
    is a waste, and the intro has its own case in tools/conformance.py.
    """
    src = os.path.join(ROOT, 'game')
    for dirpath, dirs, files in os.walk(src):
        out = os.path.join(d, os.path.relpath(dirpath, src))
        os.makedirs(out, exist_ok=True)
        for f in files:
            t = os.path.join(out, f)
            if f.upper() == 'RA2MD.INI':
                if os.path.exists(t):
                    os.remove(t)
                text = open(os.path.join(dirpath, f), 'rb').read()
                text = re.sub(rb'(?im)^Play=\w+', b'Play=no', text)
                open(t, 'wb').write(text)
            elif not os.path.exists(t):
                os.link(os.path.join(dirpath, f), t)
    return d


def run(name, args, seconds, expect, every, original=False):
    d = os.path.join(OUT, name + ('.original' if original else ''))
    if os.path.isdir(os.path.join(d, 'game')):
        shutil.rmtree(os.path.join(d, 'game'))      # a fresh folder: no saves left over
    os.makedirs(d, exist_ok=True)
    game = farm(os.path.join(d, 'game'))
    mp4, log = os.path.join(d, 'run.mp4'), os.path.join(d, 'run.log')
    cmd = [HOST, '--headless', '--run', '--debuglog', '--watchdog', str(seconds),
           '--record', mp4, '--exe', os.path.join(game, 'gamemd.exe'), '--game', game] + args
    if original:
        cmd.append('--original')
    with open(log, 'w', errors='replace') as f:
        try:
            code = subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT,
                                  timeout=seconds + 300).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    text = open(log, errors='replace').read()
    if os.path.exists(mp4):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', mp4, '-vf',
                        'fps=1/%g,scale=320:-1,tile=4x4' % max(every, seconds / 16.0), '-frames:v', '1',
                        os.path.join(d, 'sheet.png')], cwd=ROOT)
    lines = text.splitlines()
    bad = [l for l in lines if l.startswith(('===', '[not-lifted]', 'ITAIL', '[messagebox]'))]
    bad += [l for l in lines if l.startswith('[input]') and ('never opened' in l or 'no such control' in l)]
    opened = []
    for m in re.finditer(r'\[dialog\] open 0x([0-9A-F]+)', text):
        if not opened or opened[-1] != m.group(1):
            opened.append(m.group(1))
    distinct = len(set(re.findall(r'\[record\] frame \d+ (?:at \S+ )?checksum ([0-9A-F]{8})', text)))
    ingame = '[game] Capture_Mouse()' in text
    defeated = re.search(r'\[game\] MPlayer_Defeated\(\) - Player <human player> has been defeated', text)
    seen = ['dialogs ' + ' '.join(opened)] if opened else []
    if ingame:
        seen.append('in game, %d distinct frames' % distinct)
    if defeated:
        seen.append('human defeated')
    for dlg in expect.get('dialogs', []):
        if '%X' % dlg not in opened:
            bad.append('dialog 0x%X never opened' % dlg)
    if expect.get('ingame') and not (ingame and distinct >= 5):
        bad.append('never in game' if not ingame else 'picture stopped (%d frames)' % distinct)
    if expect.get('alive') and defeated:
        bad.append('the human player was defeated')
    if 'exit' in expect and code != expect['exit']:
        bad.append('expected exit %d' % expect['exit'])
    elif 'exit' not in expect and code not in (0, 4):
        bad.append('exit %s' % code)
    return name, code, bad, seen


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cases', nargs='*', help='case names or globs (default: all)')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--jobs', type=int, default=3)
    ap.add_argument('--every', type=float, default=5, help='contact-sheet spacing, seconds')
    ap.add_argument('--original', action='store_true', help='run the shipping code instead (oracle.c)')
    a = ap.parse_args()
    if a.list:
        for n, (args, s, e) in CASES.items():
            print('%-20s %4ds  %s' % (n, s, ' '.join(args)))
        return 0
    if not (os.path.exists(HOST) and os.path.isdir(os.path.join(ROOT, 'game'))):
        print('playtest: skipped -- needs game/ (your copy) and build/ra2.exe (README)')
        return 0
    names = [n for n in CASES if not a.cases or any(fnmatch.fnmatch(n, p) for p in a.cases)]
    failed = 0
    os.makedirs(OUT, exist_ok=True)
    summary = open(os.path.join(OUT, 'summary%s.txt' % ('.original' if a.original else '')), 'w')
    with ThreadPoolExecutor(a.jobs) as ex:
        for name, code, bad, seen in ex.map(lambda n: run(n, *CASES[n], a.every, a.original), names):
            name += '.original' if a.original else ''
            line = '%s %-20s exit %-7s %s%s' % ('FAIL' if bad else 'pass', name, code, '; '.join(seen) or '-',
                                                ('  !! ' + ' | '.join(bad[:3])) if bad else '')
            print(line, flush=True)
            summary.write(line + '\n')
            summary.flush()
            failed += bool(bad)
    print('%d of %d failed' % (failed, len(names)))
    summary.write('%d of %d failed\n' % (failed, len(names)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
