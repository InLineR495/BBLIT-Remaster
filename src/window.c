// M2 — окно без рамки (I6) и пилларбокс движковой картинки (C12).
// CreateWindowExA для класса "BBLIT Game" -> WS_POPUP на весь основной монитор (размер — от Wine,
// в KDE с масштабом это логические пиксели). SetWindowPos этого окна не двигает и не меняет размер.
// glViewport движка (только @0x40dbe9) вписывается в клиентскую область по центру с его пропорцией: картинка
// движка (заставки, загрузочные экраны, render=0) не зависит от /x /y запуска — при /x1600 /y1200 в окне
// 1129×706 без вписывания была видна только часть заставки.
#include <windows.h>
#include <stdint.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"
#include "window.h"

HWND bb_game_hwnd;

static HWND (WINAPI *real_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static BOOL (WINAPI *real_SetWindowPos)(HWND, HWND, int, int, int, int, UINT);

static HWND WINAPI hook_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                                        HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    int game = HIWORD((DWORD_PTR)cls) && lstrcmpA(cls, "BBLIT Game") == 0;
    MONITORINFO mi = { .cbSize = sizeof mi };
    HWND hwnd;

    if (game && bb_cfg.borderless && GetMonitorInfoA(MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi)) {
        bb_log("I6: CreateWindowExA(\"%s\") ex=%#lx style=%#lx %d,%d %dx%d -> WS_POPUP %ld,%ld %ldx%ld",
               cls, ex, style, x, y, w, h, mi.rcMonitor.left, mi.rcMonitor.top,
               mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
        ex &= ~(DWORD)(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME);
        style = WS_POPUP | WS_VISIBLE | (style & (WS_CLIPSIBLINGS | WS_CLIPCHILDREN));
        x = mi.rcMonitor.left;
        y = mi.rcMonitor.top;
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }
    hwnd = real_CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    if (game) {
        bb_game_hwnd = hwnd;
        bb_log("I6: окно игры %p", (void *)hwnd);
    }
    return hwnd;
}

static BOOL WINAPI hook_SetWindowPos(HWND hwnd, HWND after, int x, int y, int w, int h, UINT flags)
{
    if (bb_cfg.borderless && hwnd && hwnd == bb_game_hwnd) {
        bb_log("I6: SetWindowPos(игра, %d,%d %dx%d, %#x) -> без смены размера и места", x, y, w, h, flags);
        flags |= SWP_NOMOVE | SWP_NOSIZE;
    }
    return real_SetWindowPos(hwnd, after, x, y, w, h, flags);
}

// C12: вызывается из обёртки glViewport (glrec.c).
void bb_window_viewport(int *x, int *y, int *w, int *h)
{
    RECT rc;
    if (!bb_cfg.borderless || !bb_game_hwnd || !GetClientRect(bb_game_hwnd, &rc) || *w <= 0 || *h <= 0)
        return;
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    int nw = cw, nh = (int)((long long)cw * *h / *w);
    if (nh > ch) {
        nh = ch;
        nw = (int)((long long)ch * *w / *h);
    }
    int nx = (cw - nw) / 2, ny = (ch - nh) / 2;
    bb_log("C12: glViewport(%d,%d %dx%d) в клиенте %dx%d -> (%d,%d %dx%d)", *x, *y, *w, *h, cw, ch, nx, ny, nw, nh);
    *x = nx;
    *y = ny;
    *w = nw;
    *h = nh;
}

void bb_window_init(void)
{
    if (!bb_cfg.borderless)
        return;
    bb_iat_hook(BB_IAT_CREATEWINDOWEXA, (void *)hook_CreateWindowExA, (void **)&real_CreateWindowExA);
    bb_iat_hook(BB_IAT_SETWINDOWPOS, (void *)hook_SetWindowPos, (void **)&real_SetWindowPos);
}
