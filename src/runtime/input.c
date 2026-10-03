/*
 * Scripted input for headless runs (docs/testing.md).
 *
 *   --press DLG:CTRL@s   press control CTRL of menu dialog DLG, once DLG is open
 *   --select DLG:CTRL=N@s  pick item N of a list or combo box in DLG
 *   --waitlog TEXT@s     hold until the game's debug log prints a line with TEXT
 *   --move x,y@s  --click x,y@s  --key [c][s][a]+vk@s  --wait VA@s
 *
 * Civilization III's grammar and timing (civ3 src/runtime/input.c): s is
 * seconds after the main menu opened, events run in time order on one thread,
 * and a wait moves every later event back by however long it took. Nothing
 * touches the real cursor or keyboard: input is posted window messages, and
 * GetCursorPos/GetKeyState/GetAsyncKeyState answer from the script.
 *
 * What RA2 adds is --press. Its menus are Win32 dialogs from the exe's
 * resources (tools/dialogs.py maps all 98), so a script names a button by
 * dialog and control ID instead of a pixel, and waits for the dialog to be
 * open rather than for a time. The host learns each dialog's resource ID from
 * the FindResourceA(RT_DIALOG) the game makes just before creating it.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"

typedef struct { char kind; int x, y; double t; char text[64]; } ev_t;
#define MAX_EV 256
static ev_t g_ev[MAX_EV];
static int g_nev;
static volatile LONG g_cx = -1, g_cy = -1;

int input_arg(int argc, char** argv, int i) {
    if (i + 1 >= argc || g_nev >= MAX_EV) return 0;
    ev_t e = { 0 };
    const char* a = argv[i + 1];
    if (!strcmp(argv[i], "--move") || !strcmp(argv[i], "--click")) {
        if (sscanf(a, "%d,%d@%lf", &e.x, &e.y, &e.t) != 3) return 0;
        e.kind = argv[i][2];                    /* 'm' or 'c' */
    } else if (!strcmp(argv[i], "--press")) {
        if (sscanf(a, "%i:%i@%lf", &e.x, &e.y, &e.t) != 3) return 0;
        e.kind = 'p';
    } else if (!strcmp(argv[i], "--select")) {
        int n;
        if (sscanf(a, "%i:%i=%d@%lf", &e.x, &e.y, &n, &e.t) != 4) return 0;
        e.y |= n << 16;                 /* item index in the high half */
        e.kind = 's';
    } else if (!strcmp(argv[i], "--waitlog")) {
        /* Burnout 3's state signal (xboxrecomp's [PATH] lines): what the game
         * says it is doing, not how long it has taken. "Capture_Mouse" is
         * printed when a game starts. */
        const char* at = strrchr(a, '@');
        if (!at || at - a >= (int)sizeof e.text || sscanf(at + 1, "%lf", &e.t) != 1) return 0;
        memcpy(e.text, a, at - a);
        e.kind = 'l';
    } else if (!strcmp(argv[i], "--wait")) {
        /* --wait VA@s: hold the script until the dword at VA is nonzero. */
        if (sscanf(a, "%i@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'w';
    } else if (!strcmp(argv[i], "--key")) {
        const char* plus = strchr(a, '+');
        if (plus) {
            for (const char* m = a; m < plus; m++)
                e.y |= *m == 'c' ? 1 : *m == 's' ? 2 : *m == 'a' ? 4 : 0;
            a = plus + 1;
        }
        if (sscanf(a, "%i@%lf", &e.x, &e.t) != 2) return 0;
        e.kind = 'k';
    } else return 0;
    g_ev[g_nev++] = e;
    return 2;
}

/* ---- open dialogs ---------------------------------------------------------
 * A small table of (window, resource ID). The host reports each creation; a
 * poller notices when one goes away. Both print [dialog] lines, which are the
 * milestones tools/playtest.py and tools/conformance.py read. */
#define MAX_DLG 32
static struct { HWND h; int id; } g_dlg[MAX_DLG];
static CRITICAL_SECTION g_dlg_lock;
static volatile LONG g_menu_open;      /* the main menu (0xE2) has opened once */
static DWORD g_t0;

void input_dialog_created(HWND h, int id) {
    EnterCriticalSection(&g_dlg_lock);
    for (int i = 0; i < MAX_DLG; i++)
        if (!g_dlg[i].h || !IsWindow(g_dlg[i].h)) { g_dlg[i].h = h; g_dlg[i].id = id; break; }
    LeaveCriticalSection(&g_dlg_lock);
    InterlockedIncrement(&g_dialogs_opened);
    fprintf(stderr, "[dialog] open 0x%X\n", id);
    if (id == 0xE2 && !InterlockedExchange(&g_menu_open, 1)) g_t0 = GetTickCount();
}

static HWND find_dialog(int id) {
    HWND h = NULL;
    EnterCriticalSection(&g_dlg_lock);
    for (int i = 0; i < MAX_DLG && !h; i++)
        if (g_dlg[i].id == id && g_dlg[i].h && IsWindow(g_dlg[i].h)) h = g_dlg[i].h;
    LeaveCriticalSection(&g_dlg_lock);
    return h;
}

static DWORD WINAPI dialog_poller(LPVOID unused) {
    (void)unused;
    for (;;) {
        Sleep(100);
        EnterCriticalSection(&g_dlg_lock);
        for (int i = 0; i < MAX_DLG; i++)
            if (g_dlg[i].h && !IsWindow(g_dlg[i].h)) {
                fprintf(stderr, "[dialog] closed 0x%X\n", g_dlg[i].id);
                g_dlg[i].h = NULL;
            }
        LeaveCriticalSection(&g_dlg_lock);
    }
}

/* ---- the game's debug log (host.c, ra2_hook_004068E0) ------------------- */
static const char* volatile g_want_log;     /* what --waitlog waits for */
static volatile LONG g_saw_log;

int input_wants_log(void) { return g_want_log != NULL; }

void input_log_line(const char* line) {
    const char* w = g_want_log;
    if (w && strstr(line, w)) InterlockedExchange(&g_saw_log, 1);
}

/* ---- what the game reads --------------------------------------------------- */
/* A display mode change keeps the cursor on the screen, as Windows does. The
 * menus are 800x600 and a game 640x480: a cursor left on a menu button at
 * x=720 was past the right edge in game, and the game's edge scrolling ran the
 * view to the black margin of the map and kept it there (docs/testing.md). */
void input_mode_changed(int w, int h) {
    if (g_cx >= w) InterlockedExchange(&g_cx, w - 1);
    if (g_cy >= h) InterlockedExchange(&g_cy, h - 1);
}

BOOL input_cursor(POINT* p) {
    if (g_cx < 0) return FALSE;
    p->x = g_cx;            /* client = screen for the game (host.c, ClientToScreen) */
    p->y = g_cy;
    return TRUE;
}

static volatile LONG g_mods, g_lb_reads;
SHORT input_key_state(int vk, SHORT real) {
    if (!g_nev) return real;
    if (vk == VK_LBUTTON) InterlockedIncrement(&g_lb_reads);
    int bit = vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ? 1
            : vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ? 2
            : vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ? 4
            : vk == VK_LBUTTON ? 8 : 0;
    /* A scripted run answers every key from the script: the console's (or the
     * RDP phone's) real keyboard never leaks into a test. */
    return (bit && (g_mods & bit)) ? (SHORT)0x8000 : 0;
}

/* Wait until the game has read the left button twice more, up to 1 s. */
static void lb_seen(void) {
    LONG r0 = g_lb_reads;
    for (int i = 0; i < 40 && g_lb_reads - r0 < 2; i++) Sleep(25);
}

/* Held until the game has seen it down, then up (civ3: a fixed hold was
 * missed when several runs at once stretched a frame past it). */
static void click(HWND h, LPARAM lp) {
    InterlockedOr(&g_mods, 8);
    PostMessageA(h, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(250);
    lb_seen();
    PostMessageA(h, WM_LBUTTONUP, 0, lp);
    InterlockedAnd(&g_mods, ~8);
    lb_seen();
}

/* Press a dialog control: point the cursor at it and click it, the way a
 * player does. If the dialog is still open and nothing new opened 3 s later,
 * send the BN_CLICKED a button sends its parent. Returns the method used, or
 * NULL if the dialog never opened. */
static const char* press(int dlg, int ctrl, LONG* shift) {
    DWORD w0 = GetTickCount();
    HWND d;
    while (!(d = find_dialog(dlg)) && GetTickCount() - w0 < 600000) Sleep(100);
    *shift += (LONG)(GetTickCount() - w0);
    if (!d) return NULL;
    Sleep(1500);                        /* the menus slide their buttons in */
    HWND c = GetDlgItem(d, ctrl);
    if (!c) return "no such control";
    RECT r;
    GetWindowRect(c, &r);
    POINT mid = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 }, local = mid;
    MapWindowPoints(NULL, g_input_hwnd, &mid, 1);          /* game client */
    /* A static control answers WM_NCHITTEST with HTTRANSPARENT, so a real
     * click on it lands on the dialog underneath: the campaign screen's
     * emblems are statics, and the dialog procedure reads the click there. */
    char cls[16] = "";
    GetClassNameA(c, cls, sizeof cls);
    if (!_stricmp(cls, "Static")) c = d;
    ScreenToClient(c, &local);
    InterlockedExchange(&g_cx, mid.x);
    InterlockedExchange(&g_cy, mid.y);
    PostMessageA(c, WM_MOUSEMOVE, 0, MAKELPARAM(local.x, local.y));
    Sleep(200);
    LONG opens = g_dialogs_opened;
    click(c, MAKELPARAM(local.x, local.y));
    for (int i = 0; i < 30; i++) {
        if (!IsWindow(d) || g_dialogs_opened != opens) return "click";
        Sleep(100);
    }
    PostMessageA(d, WM_COMMAND, MAKEWPARAM(ctrl, BN_CLICKED), (LPARAM)c);
    return "BN_CLICKED";
}

/* Pick item N of a list or combo box: set the selection and send the parent
 * the notification a click on the item sends (LBN_SELCHANGE / CBN_SELCHANGE),
 * so the dialog procedure reacts as it would to the player. */
static const char* select_item(int dlg, int ctrl, int n, LONG* shift) {
    DWORD w0 = GetTickCount();
    HWND d;
    char cls[32] = "";
    while (!(d = find_dialog(dlg)) && GetTickCount() - w0 < 600000) Sleep(100);
    *shift += (LONG)(GetTickCount() - w0);
    if (!d) return NULL;
    Sleep(1500);
    HWND c = GetDlgItem(d, ctrl);
    if (!c) return "no such control";
    GetClassNameA(c, cls, sizeof cls);
    int combo = !_stricmp(cls, "ComboBox");
    SendMessageA(c, combo ? CB_SETCURSEL : LB_SETCURSEL, n, 0);
    PostMessageA(d, WM_COMMAND, MAKEWPARAM(ctrl, combo ? CBN_SELCHANGE : LBN_SELCHANGE), (LPARAM)c);
    return combo ? "combo" : "list";
}

static int cmp_ev(const void* a, const void* b) {
    double d = ((const ev_t*)a)->t - ((const ev_t*)b)->t;
    return d < 0 ? -1 : d > 0;
}

static DWORD WINAPI script(LPVOID unused) {
    (void)unused;
    while (!g_menu_open) Sleep(50);
    LONG shift = 0;                     /* ms the script runs late: waits */
    for (int i = 0; i < g_nev; i++) {
        ev_t* e = &g_ev[i];
        LONG wait = (LONG)(e->t * 1000) + shift - (LONG)(GetTickCount() - g_t0);
        if (wait > 0) Sleep(wait);
        HWND h = g_input_hwnd;
        if (e->kind == 'p') {
            const char* how = press(e->x, e->y, &shift);
            fprintf(stderr, "[input] %.1fs press 0x%X:%d -> %s\n", e->t, e->x, e->y,
                    how ? how : "dialog never opened");
            continue;
        }
        if (e->kind == 's') {
            const char* how = select_item(e->x, e->y & 0xFFFF, e->y >> 16, &shift);
            fprintf(stderr, "[input] %.1fs select 0x%X:%d=%d -> %s\n", e->t, e->x, e->y & 0xFFFF,
                    e->y >> 16, how ? how : "dialog never opened");
            continue;
        }
        if (e->kind == 'l') {
            DWORD w0 = GetTickCount();
            InterlockedExchange(&g_saw_log, 0);
            g_want_log = e->text;
            while (!g_saw_log && GetTickCount() - w0 < 600000) Sleep(50);
            g_want_log = NULL;
            shift += (LONG)(GetTickCount() - w0);
            fprintf(stderr, "[input] %.1fs waited %.1fs for log \"%s\"%s\n", e->t,
                    (GetTickCount() - w0) / 1000.0, e->text, g_saw_log ? "" : " -- never printed");
            continue;
        }
        if (e->kind == 'w') {
            DWORD w0 = GetTickCount();
            while (!*(volatile uint32_t*)(uintptr_t)e->x && GetTickCount() - w0 < 300000) Sleep(100);
            shift += (LONG)(GetTickCount() - w0);
            fprintf(stderr, "[input] %.1fs waited %.1fs for [0x%X] = %u\n", e->t,
                    (GetTickCount() - w0) / 1000.0, e->x, *(volatile uint32_t*)(uintptr_t)e->x);
            continue;
        }
        if (e->kind == 'k') {
            static const int mvk[] = { VK_CONTROL, VK_SHIFT, VK_MENU };
            fprintf(stderr, "[input] %.1fs key %s%s%s0x%X\n", e->t, e->y & 1 ? "Ctrl-" : "",
                    e->y & 2 ? "Shift-" : "", e->y & 4 ? "Alt-" : "", e->x);
            for (int m = 0; m < 3; m++)
                if (e->y & 1 << m) PostMessageA(h, WM_KEYDOWN, mvk[m], 1);
            InterlockedExchange(&g_mods, e->y);
            PostMessageA(h, WM_KEYDOWN, e->x, 1);
            Sleep(150);
            PostMessageA(h, WM_KEYUP, e->x, 0xC0000001);
            Sleep(500);         /* ponytail: the handler reads the modifiers when it runs; a busy frame longer than this would miss them */
            InterlockedExchange(&g_mods, 0);
            for (int m = 0; m < 3; m++)
                if (e->y & 1 << m) PostMessageA(h, WM_KEYUP, mvk[m], 0xC0000001);
            continue;
        }
        LPARAM lp = MAKELPARAM(e->x, e->y);
        fprintf(stderr, "[input] %.1fs %s %d,%d\n", e->t, e->kind == 'c' ? "click" : "move", e->x, e->y);
        InterlockedExchange(&g_cx, e->x);
        InterlockedExchange(&g_cy, e->y);
        PostMessageA(h, WM_MOUSEMOVE, 0, lp);
        if (e->kind == 'c') {
            Sleep(100);
            click(h, lp);
        }
    }
    fprintf(stderr, "[input] script done\n");
    return 0;
}

HWND g_input_hwnd;
volatile LONG g_dialogs_opened;

void input_start(void) {
    InitializeCriticalSection(&g_dlg_lock);
    CloseHandle(CreateThread(NULL, 0, dialog_poller, NULL, 0, NULL));
    if (!g_nev) return;
    qsort(g_ev, g_nev, sizeof *g_ev, cmp_ev);
    CloseHandle(CreateThread(NULL, 0, script, NULL, 0, NULL));
}

int input_scripted(void) { return g_nev > 0; }
