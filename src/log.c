// Лог в remaster.log рядом с DLL. Только WinAPI (без FILE* и кучи CRT): у игры свой статический CRT.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "log.h"

static HANDLE log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_lock;
static LARGE_INTEGER qpc_freq, qpc_start;

void bb_log_open(HINSTANCE self)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        n--;
    lstrcpyA(path + n, "remaster.log");
    InitializeCriticalSection(&log_lock);
    QueryPerformanceFrequency(&qpc_freq);
    QueryPerformanceCounter(&qpc_start);
    log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void bb_log_close(void)
{
    if (log_file != INVALID_HANDLE_VALUE)
        CloseHandle(log_file);
    log_file = INVALID_HANDLE_VALUE;
}

volatile int bb_log_mute;               // секундные строки P* в тихом режиме (log_sec=0) не пишутся

void bb_log(const char *fmt, ...)
{
    char buf[1024];
    LARGE_INTEGER now;
    va_list ap;
    int len;
    DWORD written;

    if (log_file == INVALID_HANDLE_VALUE || bb_log_mute)
        return;
    QueryPerformanceCounter(&now);
    len = _snprintf(buf, sizeof buf, "[%9.3f] ",
                    (double)(now.QuadPart - qpc_start.QuadPart) / (double)qpc_freq.QuadPart);
    va_start(ap, fmt);
    len += _vsnprintf(buf + len, sizeof buf - len - 2, fmt, ap);
    va_end(ap);
    if (len < 0 || len > (int)sizeof buf - 2)
        len = sizeof buf - 2;
    buf[len++] = '\r';
    buf[len++] = '\n';
    EnterCriticalSection(&log_lock);
    // без FlushFileBuffers: это fsync на каждую строку (15–100 мс на btrfs) — он и давал статтеры в тике.
    // Данные после WriteFile уже в кэше ОС и переживают падение игры.
    WriteFile(log_file, buf, len, &written, NULL);
    LeaveCriticalSection(&log_lock);
}
