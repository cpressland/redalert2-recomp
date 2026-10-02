/*
 * Red Alert 2: Yuri's Revenge - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header). What is here is only what is specific to
 * this game: where the image goes, the command line, headless DirectDraw, and
 * the fault report. docs/host.md has the reasoning.
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so 0x00400000..0x00B7A000 is
 * free when main() maps the image.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddraw.h>

#include "native32.h"
#include "recomp_trace.h"

extern const uint32_t ra2_entry_va;    /* recomp_dispatch.c */

#define RA2_IMAGE_BASE 0x00400000u

static DWORD g_watchdog_s;
static int   g_headless;

#define ARG(n) MEM32(g_esp + 4 + 4 * (n))
static const char* gstr(uint32_t va) { return va ? (const char*)(uintptr_t)va : "(null)"; }

/* ---- headless ------------------------------------------------------------
 * Nothing reaches the screen. Over RDP a window lands on whatever device is
 * connected and a display-mode change resizes it (REPO_RULES section 13), so
 * message boxes print, the window is created hidden, and DirectDraw is kept
 * from going fullscreen: see "headless DirectDraw" below. */

static uint32_t mb_answer(uint32_t type) {
    uint32_t buttons = type & MB_TYPEMASK;
    return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? IDNO : IDOK;
}

static HWND g_game_hwnd;
static DWORD g_mode_w = 640, g_mode_h = 480, g_mode_bpp = 16;
static int g_mode_set;

static void shim_MessageBoxA(void) {
    g_eax = mb_answer(ARG(3));
    fprintf(stderr, "[messagebox] type 0x%X -> %u: %s: %s\n", ARG(3), g_eax, gstr(ARG(2)), gstr(ARG(1)));
    g_esp += 4 + 4 * 4;
}

static void shim_CreateWindowExA(void) {
    HWND h = CreateWindowExA(ARG(0), (LPCSTR)(uintptr_t)ARG(1), (LPCSTR)(uintptr_t)ARG(2),
                             ARG(3) & ~WS_VISIBLE, (int)ARG(4), (int)ARG(5), (int)ARG(6),
                             (int)ARG(7), (HWND)(uintptr_t)ARG(8), (HMENU)(uintptr_t)ARG(9),
                             (HINSTANCE)(uintptr_t)ARG(10), (LPVOID)(uintptr_t)ARG(11));
    fprintf(stderr, "[headless] CreateWindowExA(\"%s\", %dx%d) from sub_%08X -> hidden hwnd %p\n",
            gstr(ARG(2)), (int)ARG(6), (int)ARG(7), g_cur_func, (void*)h);
    if (!g_game_hwnd) g_game_hwnd = h;
    g_eax = (uint32_t)(uintptr_t)h;
    g_esp += 4 + 12 * 4;
}

/* The game was written for an exclusive fullscreen window, whose client
 * area is the screen, and it offsets drawing into the primary by the window's
 * screen position (the Bink intro, 0x00432EF0). Headless, the primary is an
 * offscreen surface and the hidden window sits wherever Windows put it, so the
 * client-to-screen mapping is identity, as it was in fullscreen. */
static void shim_ClientToScreen(void) {
    g_eax = 1;
    g_esp += 4 + 2 * 4;
}

/* ...and the screen it reports is the mode's, as it would be after a real
 * mode change. Before the first SetDisplayMode it is the real desktop. */
static void shim_GetSystemMetrics(void) {
    int i = (int)ARG(0);
    g_eax = (uint32_t)GetSystemMetrics(i);
    if (g_mode_set && i == SM_CXSCREEN) g_eax = g_mode_w;
    if (g_mode_set && i == SM_CYSCREEN) g_eax = g_mode_h;
    g_esp += 4 + 1 * 4;
}

/* Headless movies play without sound. With it, the intro froze for good
 * about 40 s in, in both runs; without it, it froze once at ~110 s and once
 * not at all in 150 s. So sound makes a stall in Bink's pacing reliable rather
 * than causing it. docs/bringup.md, 6.
 * ponytail: this narrows the stall, it does not fix it; find why Bink stops
 * advancing, then record the audio too. */
static void shim_BinkSetSoundSystem(void) {
    fprintf(stderr, "[headless] BinkSetSoundSystem refused: movies play silent\n");
    g_eax = 0;
    g_esp += 4 + 2 * 4;
}

static void shim_ShowWindow(void) {
    g_eax = 0;                         /* "was hidden", which is true */
    g_esp += 4 + 2 * 4;
}

/* ---- headless DirectDraw -------------------------------------------------
 * The game asks for an exclusive fullscreen 16-bit mode and draws by blitting
 * into the primary surface. Headless, the real DirectDraw object is kept and
 * three of its methods are patched in the (shared) IDirectDraw vtable:
 *
 *   SetCooperativeLevel  -> DDSCL_NORMAL, so nothing is exclusive
 *   SetDisplayMode       -> remembered, not applied
 *   GetDisplayMode       -> answers the remembered mode
 *   CreateSurface        -> the primary becomes an offscreen system-memory
 *                           surface of that mode; every surface without its own
 *                           pixel format gets the mode's, which a 32-bit
 *                           desktop would otherwise hand out
 *
 * The game never learns the difference: it blits into "the primary" and that
 * surface is what --record reads. */
static IDirectDrawSurface* g_primary;
static volatile LONG g_frames;       /* blits into the primary */

static const char* g_record;
static long g_record_frames;

typedef HRESULT (WINAPI *dd_coop_t)(IDirectDraw*, HWND, DWORD);
typedef HRESULT (WINAPI *dd_mode_t)(IDirectDraw*, DWORD, DWORD, DWORD);
typedef HRESULT (WINAPI *dd_getmode_t)(IDirectDraw*, LPDDSURFACEDESC);
typedef HRESULT (WINAPI *dd_surf_t)(IDirectDraw*, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*, IUnknown*);
typedef HRESULT (WINAPI *dds_blt_t)(IDirectDrawSurface*, LPRECT, IDirectDrawSurface*, LPRECT, DWORD, LPDDBLTFX);
static dd_coop_t g_real_coop;
static dd_mode_t g_real_mode;
static dd_getmode_t g_real_getmode;
static dd_surf_t g_real_surf;
static dds_blt_t g_real_blt;

static void mode_format(DDPIXELFORMAT* pf) {
    memset(pf, 0, sizeof *pf);
    pf->dwSize = sizeof *pf;
    pf->dwFlags = DDPF_RGB;
    pf->dwRGBBitCount = g_mode_bpp;
    if (g_mode_bpp == 16) { pf->dwRBitMask = 0xF800; pf->dwGBitMask = 0x07E0; pf->dwBBitMask = 0x001F; }
    else { pf->dwRBitMask = 0xFF0000; pf->dwGBitMask = 0x00FF00; pf->dwBBitMask = 0x0000FF; }
}

static HRESULT WINAPI hl_SetCooperativeLevel(IDirectDraw* dd, HWND h, DWORD flags) {
    HRESULT hr = g_real_coop(dd, h, DDSCL_NORMAL);
    fprintf(stderr, "[headless] SetCooperativeLevel(0x%lX) -> NORMAL: 0x%08lX\n", flags, hr);
    return hr;
}

static HRESULT WINAPI hl_SetDisplayMode(IDirectDraw* dd, DWORD w, DWORD h, DWORD bpp) {
    (void)dd;
    g_mode_w = w, g_mode_h = h, g_mode_bpp = bpp, g_mode_set = 1;
    /* A real mode change resizes the fullscreen window to the new screen;
     * the game then sizes and centres things by it (the Bink intro). */
    if (g_game_hwnd) SetWindowPos(g_game_hwnd, NULL, 0, 0, (int)w, (int)h, SWP_NOZORDER | SWP_NOACTIVATE);
    fprintf(stderr, "[headless] SetDisplayMode(%lux%lux%lu) -> kept, not applied\n", w, h, bpp);
    return DD_OK;
}

static HRESULT WINAPI hl_GetDisplayMode(IDirectDraw* dd, LPDDSURFACEDESC d) {
    HRESULT hr = g_real_getmode(dd, d);
    if (hr == DD_OK) {
        d->dwWidth = g_mode_w, d->dwHeight = g_mode_h;
        d->lPitch = g_mode_w * (g_mode_bpp / 8);
        mode_format(&d->ddpfPixelFormat);
    }
    return hr;
}

/* Every blit into the primary is a frame; the count is the boot's milestone. */
static HRESULT WINAPI hl_Blt(IDirectDrawSurface* dst, LPRECT r, IDirectDrawSurface* src, LPRECT sr,
                             DWORD flags, LPDDBLTFX fx) {
    if (dst == g_primary) {
        LONG n = InterlockedIncrement(&g_frames);
        if (n == 1 || n == 10 || n == 100 || n % 1000 == 0)
            fprintf(stderr, "[headless] frame %ld blitted to the primary\n", n);
    }
    return g_real_blt(dst, r, src, sr, flags, fx);
}

static HRESULT WINAPI hl_CreateSurface(IDirectDraw* dd, LPDDSURFACEDESC d, LPDIRECTDRAWSURFACE* out,
                                       IUnknown* outer) {
    DDSURFACEDESC c = *d;
    int primary = (d->dwFlags & DDSD_CAPS) && (d->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE);
    if (primary) {
        c.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        c.dwWidth = g_mode_w, c.dwHeight = g_mode_h;
        c.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        mode_format(&c.ddpfPixelFormat);
    } else if (!(d->dwFlags & DDSD_PIXELFORMAT)) {
        c.dwFlags |= DDSD_PIXELFORMAT;
        mode_format(&c.ddpfPixelFormat);
        /* Video memory will not take a format unlike the desktop's. */
        c.ddsCaps.dwCaps = (c.ddsCaps.dwCaps & ~(DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM))
                         | DDSCAPS_SYSTEMMEMORY;
        /* A surface with no type takes the display's format; one given a
         * format has to say it is offscreen, or DDERR_INVALIDPIXELFORMAT. */
        if (!(c.ddsCaps.dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_OVERLAY | DDSCAPS_ZBUFFER)))
            c.ddsCaps.dwCaps |= DDSCAPS_OFFSCREENPLAIN;
        c.dwFlags |= DDSD_CAPS;
    }
    HRESULT hr = g_real_surf(dd, &c, out, outer);
    fprintf(stderr, "[headless] CreateSurface(flags 0x%lX caps 0x%lX %lux%lu)%s -> 0x%08lX %p\n",
            d->dwFlags, d->ddsCaps.dwCaps, c.dwWidth, c.dwHeight, primary ? " primary" : "", hr,
            hr == DD_OK ? (void*)*out : NULL);
    if (hr == DD_OK && primary) {
        g_primary = *out;
        if (!g_real_blt) {
            void** vt = *(void***)g_primary;
            DWORD old;
            g_real_blt = (dds_blt_t)vt[5];                     /* IDirectDrawSurface::Blt */
            VirtualProtect(&vt[5], 4, PAGE_READWRITE, &old);
            vt[5] = (void*)hl_Blt;
            VirtualProtect(&vt[5], 4, old, &old);
        }
    }
    return hr;
}

static void patch(void** vt, int slot, void* fn, void** real) {
    DWORD old;
    *real = vt[slot];
    VirtualProtect(&vt[slot], 4, PAGE_READWRITE, &old);
    vt[slot] = fn;
    VirtualProtect(&vt[slot], 4, old, &old);
}

static void shim_DirectDrawCreate(void) {
    GUID* guid = (GUID*)(uintptr_t)ARG(0);
    IDirectDraw** out = (IDirectDraw**)(uintptr_t)ARG(1);
    HRESULT hr = DirectDrawCreate(guid, out, (IUnknown*)(uintptr_t)ARG(2));
    if (hr == DD_OK && !g_real_surf) {
        void** vt = *(void***)*out;
        patch(vt, 6, (void*)hl_CreateSurface, (void**)&g_real_surf);
        patch(vt, 12, (void*)hl_GetDisplayMode, (void**)&g_real_getmode);
        patch(vt, 20, (void*)hl_SetCooperativeLevel, (void**)&g_real_coop);
        patch(vt, 21, (void*)hl_SetDisplayMode, (void**)&g_real_mode);
    }
    fprintf(stderr, "[headless] DirectDrawCreate -> 0x%08lX\n", hr);
    g_eax = (uint32_t)hr;
    g_esp += 4 + 3 * 4;
}

/* --record out.mp4: the primary, read at 30 fps from a host thread and piped
 * to ffmpeg as raw BGRX. Nothing is shown anywhere, so it works over RDP
 * (REPO_RULES 10/13). --frames N stops after N recorded frames and closes the
 * file properly; a process killed mid-recording leaves an mp4 with no index. */
static FILE* g_ffmpeg;
static long g_recorded;

static void record_close(void) {
    if (g_ffmpeg) {
        _pclose(g_ffmpeg);
        g_ffmpeg = NULL;
        fprintf(stderr, "[record] %ld frames -> %s\n", g_recorded, g_record);
    }
}

static DWORD WINAPI recorder(LPVOID unused) {
    static uint32_t row[4096];
    DWORD next = GetTickCount();
    (void)unused;
    for (;;) {
        DDSURFACEDESC d;
        next += 33;
        { LONG wait = (LONG)(next - GetTickCount()); if (wait > 0) Sleep(wait); }
        if (!g_primary) continue;
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        if (g_primary->lpVtbl->Lock(g_primary, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL) != DD_OK)
            continue;
        if (!g_ffmpeg) {
            char cmd[MAX_PATH * 2];
            _snprintf(cmd, sizeof cmd - 1, "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgr0 "
                      "-s %lux%lu -r 30 -i - -c:v libx264 -pix_fmt yuv420p \"%s\"",
                      d.dwWidth, d.dwHeight, g_record);
            g_ffmpeg = _popen(cmd, "wb");
            fprintf(stderr, "[record] %lux%lu %lu bpp -> %s\n", d.dwWidth, d.dwHeight,
                    d.ddpfPixelFormat.dwRGBBitCount, g_record);
        }
        /* Copy out under the lock, convert and write after it: the pipe can
         * block, and the game's own Lock of the primary waits while ours is held. */
        {
            static uint8_t frame[4096 * 2160 * 4];
            DWORD w = d.dwWidth < 4096 ? d.dwWidth : 4096, h = d.dwHeight < 2160 ? d.dwHeight : 2160;
            DWORD bpp = d.ddpfPixelFormat.dwRGBBitCount / 8, rowb = w * bpp;
            for (DWORD y = 0; y < h; y++)
                memcpy(frame + y * rowb, (const uint8_t*)d.lpSurface + y * d.lPitch, rowb);
            g_primary->lpVtbl->Unlock(g_primary, NULL);
            if (g_recorded == 0 || g_recorded % 300 == 0) {
                uint32_t sum = 0;
                for (DWORD k = 0; k < rowb * h; k += 64) sum = sum * 31 + frame[k];
                fprintf(stderr, "[record] frame %ld at %p checksum %08X\n", g_recorded, d.lpSurface, sum);
            }
            for (DWORD y = 0; y < h && g_ffmpeg; y++) {
                const uint8_t* src = frame + y * rowb;
                for (DWORD x = 0; x < w; x++) {
                    if (bpp == 2) {
                        uint16_t p = ((const uint16_t*)src)[x];
                        uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
                        row[x] = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
                    } else row[x] = ((const uint32_t*)src)[x];
                }
                fwrite(row, 4, w, g_ffmpeg);
            }
        }
        if (g_ffmpeg && ++g_recorded == g_record_frames) {
            record_close();
            fflush(stderr);
            TerminateProcess(GetCurrentProcess(), 0);
        }
    }
}

/* ---- the guest's identity ------------------------------------------------
 * The guest is gamemd.exe in the game folder, not this host. Its hInstance
 * (resources, window classes) comes from GetModuleHandleA(NULL), and it finds
 * its files relative to GetModuleFileNameA. */
static char g_guest_exe[MAX_PATH], g_guest_cmdline[MAX_PATH + 3];

static void shim_GetModuleHandleA(void) {
    g_eax = ARG(0) ? (uint32_t)(uintptr_t)GetModuleHandleA((LPCSTR)(uintptr_t)ARG(0))
                   : RA2_IMAGE_BASE;
    g_esp += 4 + 1 * 4;
}

static void shim_GetModuleFileNameA(void) {
    uint32_t h = ARG(0), size = ARG(2);
    char* out = (char*)(uintptr_t)ARG(1);
    if (h == 0 || h == RA2_IMAGE_BASE) {
        uint32_t n = (uint32_t)strlen(g_guest_exe);
        if (size) {
            uint32_t k = n < size ? n : size - 1;
            memcpy(out, g_guest_exe, k);
            out[k] = 0;
            n = k;
        }
        g_eax = n;
    } else {
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)h, out, size);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) {
    g_eax = (uint32_t)(uintptr_t)g_guest_cmdline;
    g_esp += 4;
}

/* ---- registration-free COM -----------------------------------------------
 * WinMain checks that Blowfish.dll (the Westwood Online cipher, a COM server
 * in the game folder) can be created, tries DllRegisterServer if not, and
 * exits with a fatal box if it still cannot. The Steam install script
 * registers it under HKCR, which needs admin; an unelevated self-registration
 * "succeeds" and writes nothing. So a class that is not registered is asked
 * of the game folder's own servers directly, the way a side-by-side manifest
 * would, and nothing is written to the registry. */
static const char* const g_com_servers[] = { "Blowfish.dll" };

typedef HRESULT (WINAPI *get_class_t)(REFCLSID, REFIID, void**);

static void shim_CoCreateInstance(void) {
    REFCLSID clsid = (REFCLSID)(uintptr_t)ARG(0);
    IUnknown* outer = (IUnknown*)(uintptr_t)ARG(1);
    REFIID iid = (REFIID)(uintptr_t)ARG(3);
    void** out = (void**)(uintptr_t)ARG(4);
    HRESULT hr = CoCreateInstance(clsid, outer, ARG(2), iid, out);
    for (int i = 0; hr == REGDB_E_CLASSNOTREG && i < (int)(sizeof g_com_servers / sizeof *g_com_servers); i++) {
        HMODULE m = LoadLibraryA(g_com_servers[i]);
        get_class_t get = m ? (get_class_t)GetProcAddress(m, "DllGetClassObject") : NULL;
        IClassFactory* f = NULL;
        if (get && get(clsid, &IID_IClassFactory, (void**)&f) == S_OK) {
            hr = f->lpVtbl->CreateInstance(f, outer, iid, out);
            f->lpVtbl->Release(f);
            fprintf(stderr, "[com] class %08lX not registered: served by %s -> 0x%08lX\n",
                    clsid->Data1, g_com_servers[i], hr);
        }
    }
    g_eax = (uint32_t)hr;
    g_esp += 4 + 5 * 4;
}

#define GUEST_SHIMS \
    { "CoCreateInstance", shim_CoCreateInstance }, \
    { "GetModuleHandleA", shim_GetModuleHandleA }, \
    { "GetModuleFileNameA", shim_GetModuleFileNameA }, \
    { "GetCommandLineA", shim_GetCommandLineA }

static native32_shim_t g_shims[] = { GUEST_SHIMS };

static native32_shim_t g_headless_shims[] = {
    GUEST_SHIMS,
    { "MessageBoxA", shim_MessageBoxA },
    { "CreateWindowExA", shim_CreateWindowExA },
    { "ShowWindow", shim_ShowWindow },
    { "ClientToScreen", shim_ClientToScreen },
    { "ScreenToClient", shim_ClientToScreen },
    { "GetSystemMetrics", shim_GetSystemMetrics },
    { "_BinkSetSoundSystem@8", shim_BinkSetSoundSystem },
    { "DirectDrawCreate", shim_DirectDrawCreate },
};

/* ---- diagnostics ---------------------------------------------------------
 * --probe VA (repeatable): report indirect calls to VA -- a virtual method or
 * callback -- with `this` and the first arguments. RECOMP_ICALL asks this
 * hook before the dispatch table, so it costs nothing when unset. */
#define MAX_PROBES 8
static uint32_t g_probe[MAX_PROBES];
static int g_nprobe;
static volatile LONG g_probe_hits[MAX_PROBES];

recomp_func_t recomp_lookup_manual(uint32_t va) {
    for (int i = 0; i < g_nprobe; i++)
        if (g_probe[i] == va && InterlockedIncrement(&g_probe_hits[i]) <= 5)
            fprintf(stderr, "[probe] sub_%08X from sub_%08X  ecx=%08X  args %08X %08X %08X\n",
                    va, g_cur_func, g_ecx, MEM32(g_esp), MEM32(g_esp + 4), MEM32(g_esp + 8));
    return NULL;
}

static void probe_report(void) {
    for (int i = 0; i < g_nprobe; i++)
        fprintf(stderr, "[probe] sub_%08X: %ld calls\n", g_probe[i], g_probe_hits[i]);
}

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr,
        "\n[not-lifted] sub_%08X  (called from 0x%08X)\n"
        "  Widen the closure:  py -3 run_lift.py --roots 0x%08X  (or --max N, or --all)\n",
        va, g_cur_func, va);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

/* Added after native32's own handler, so callbacks are resolved first and
 * only real faults get here. The report goes out through WriteFile from a
 * static buffer, not stdio: a fault while another thread holds the CRT's
 * stderr lock, or with no stack left, otherwise ends with no report at all
 * (The Movies, docs/bringup.md). */
static char g_crash_buf[4096];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), 3);
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode,
               r->ExceptionAddress, GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X%s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at,
                   native32_in_guest(at) ? " (inside the guest image)" : at < 0x10000 ? " (null/low)" : "");
    }
    crash_emit("  in lifted sub_%08X, last native call %s\n", g_cur_func, g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 12 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "");
    }
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X, last native call %s, %u indirect calls, %ld frames\n",
            g_watchdog_s, g_cur_func, g_cur_import, g_icall_count, g_frames);
    native32_dump_icalls(8);
    probe_report();
    record_close();
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

int main(int argc, char** argv) {
    const char* exe = "game\\gamemd.exe";
    const char* game = "game";
    char exe_full[MAX_PATH], game_full[MAX_PATH];
    int run = 0;
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--headless")) g_headless = 1;
        else if (!strcmp(argv[i], "--probe") && i + 1 < argc && g_nprobe < MAX_PROBES)
            g_probe[g_nprobe++] = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) g_record = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) g_record_frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else {
            printf("usage: ra2 [--run] [--headless] [--record out.mp4] [--frames N] [--exe game\\gamemd.exe] [--game game]\n"
                   "           [--watchdog S] [--probe VA] [--native-trace] [--callbacks]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    if (g_record && !g_headless) { fprintf(stderr, "--record needs --headless\n"); return 1; }
    GetFullPathNameA(exe, MAX_PATH, exe_full, NULL);
    GetFullPathNameA(game, MAX_PATH, game_full, NULL);
    if (g_record) {                    /* the run chdirs into game\ */
        static char rec_full[MAX_PATH];
        GetFullPathNameA(g_record, MAX_PATH, rec_full, NULL);
        g_record = rec_full;
    }
    _snprintf(g_guest_exe, sizeof g_guest_exe - 1, "%s\\gamemd.exe", game_full);
    _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1, "\"%s\"", g_guest_exe);

    /* binkw32.dll ships in the game folder, so imports bind from there. But the
     * folder also carries DDrawCompat as ddraw.dll, and that is a fullscreen
     * shim of its own: load the system's DirectDraw first, by full path, so the
     * game's DDRAW.dll import binds to it. */
    {
        char sys[MAX_PATH];
        GetSystemDirectoryA(sys, MAX_PATH);
        strcat_s(sys, sizeof sys, "\\ddraw.dll");
        if (!LoadLibraryA(sys)) fprintf(stderr, "warning: cannot load %s\n", sys);
        SetDllDirectoryA(game_full);
    }

    native32_init();
    AddVectoredExceptionHandler(0, crash);
    printf("Red Alert 2: Yuri's Revenge recomp host\n  lifted functions in dispatch: %u\n",
           recomp_dispatch_count);

    uint32_t span = native32_map(exe_full, RA2_IMAGE_BASE);
    if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, RA2_IMAGE_BASE); return 1; }
    printf("  mapped %s: 0x%08X-0x%08X\n", exe, RA2_IMAGE_BASE, RA2_IMAGE_BASE + span);
    if (g_headless ? native32_bind(RA2_IMAGE_BASE, g_headless_shims,
                                   (int)(sizeof g_headless_shims / sizeof g_headless_shims[0]))
                   : native32_bind(RA2_IMAGE_BASE, g_shims, (int)(sizeof g_shims / sizeof g_shims[0])))
        return 1;
    printf("  guest exe %s\n", g_guest_exe);

    if (!run) {
        printf("\n(dry run: image mapped and bound; --run enters 0x%08X)\n", ra2_entry_va);
        return 0;
    }
    /* The game opens its MIX files relative to its working directory. */
    if (!SetCurrentDirectoryA(game_full)) { fprintf(stderr, "cannot enter %s\n", game_full); return 1; }
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    if (g_record) CloseHandle(CreateThread(NULL, 0, recorder, NULL, 0, NULL));
    printf("  entering 0x%08X\n\n", ra2_entry_va);
    fflush(stdout);
    native32_call_guest(ra2_entry_va, 0, NULL);
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}
