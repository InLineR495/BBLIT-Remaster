// ABI-transparent forwarders for legacy Direct3D's imports from ddraw.dll.
// Resolve lazily, outside DllMain. Preserve the original stack, registers,
// and flags, then let the system function return directly to the caller.
#include <windows.h>

#define BB_LEGACY_EXPORT(name) \
    static FARPROC bb_real_##name __attribute__((used)); \
    static const char bb_name_##name[] __attribute__((used)) = #name; \
    __attribute__((naked)) void proxy_##name(void) \
    { \
        __asm__ volatile( \
            "pushfl\n" \
            "cmpl $0, _bb_real_" #name "\n" \
            "jne 1f\n" \
            "pushal\n" \
            "movl %esp, %ebx\n" \
            "andl $-16, %esp\n" \
            "subl $12, %esp\n" \
            "pushl $_bb_name_" #name "\n" \
            "call _bb_ddraw_export\n" \
            "movl %ebx, %esp\n" \
            "movl %eax, _bb_real_" #name "\n" \
            "popal\n" \
            "1: popfl\n" \
            "jmp *_bb_real_" #name "\n"); \
    }

BB_LEGACY_EXPORT(AcquireDDThreadLock)
BB_LEGACY_EXPORT(ReleaseDDThreadLock)
BB_LEGACY_EXPORT(DDInternalLock)
BB_LEGACY_EXPORT(DDInternalUnlock)
BB_LEGACY_EXPORT(D3DParseUnknownCommand)
