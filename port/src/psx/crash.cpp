// Crash reporting: logs the faulting address and a frame-pointer stack walk (symbolize the
// addresses with llvm-addr2line -f -e parappa.exe).
#include <windows.h>

#include <cstdint>
#include <cstdlib>

#include "psx/log.h"

namespace {

HANDLE g_main_thread;

// Return addresses from the saved-EBP chain (the port is built with frame pointers).
void log_stack(const CONTEXT *ctx, psx::LogLevel level = psx::LogLevel::Error) {
    const uintptr_t *fp = reinterpret_cast<const uintptr_t *>(ctx->Ebp);
    for (int i = 0; i < 32 && fp; i++) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(fp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
        uintptr_t ret = fp[1];
        psx::logf(level, "  #%02d %08X", i, unsigned(ret));
        const uintptr_t *next = reinterpret_cast<const uintptr_t *>(fp[0]);
        if (next <= fp) break;
        fp = next;
    }
}

// ---- low-address (NULL page) redirection ----------------------------------------------------
// On the PS1, addresses 0-0x1FFFFF are a mirror of main RAM, and the game reads through NULL
// pointers relying on that (e.g. "*(short *)NULL == 0" ends a list). The first 64 KB cannot be
// mapped on Windows, so a fault there is fixed up: the base (or index) register of the faulting
// instruction is moved into the KSEG0 view of the same RAM (+0x80000000) and the access retried.

// Faults anywhere in the 2 MB mirror are handled: besides the NULL page, pieces of the mirror that
// Windows had already used at start-up are left unmapped (see memory_init).
constexpr DWORD LOW_LIMIT = 0x200000;

DWORD *reg_ptr(CONTEXT *c, int r) {
    switch (r) {
    case 0: return &c->Eax;
    case 1: return &c->Ecx;
    case 2: return &c->Edx;
    case 3: return &c->Ebx;
    case 4: return &c->Esp;
    case 5: return &c->Ebp;
    case 6: return &c->Esi;
    default: return &c->Edi;
    }
}

bool has_modrm_1byte(uint8_t op) {
    if (op < 0x40) return (op & 7) < 4;  // ALU r/m forms
    switch (op) {
    case 0x62: case 0x63: case 0x69: case 0x6B: case 0xC0: case 0xC1: case 0xC4: case 0xC5: case 0xC6: case 0xC7:
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xF6: case 0xF7: case 0xFE: case 0xFF:
        return true;
    default:
        return (op >= 0x80 && op <= 0x8F) || (op >= 0xD8 && op <= 0xDF);
    }
}

// Returns true if the instruction's low address register was redirected.
bool redirect_low_access(CONTEXT *c) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(c->Eip);
    for (int i = 0; i < 4; i++) {  // prefixes
        uint8_t b = *p;
        if (b == 0x66 || b == 0xF0 || b == 0xF2 || b == 0xF3 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x26)
            p++;
        else
            break;
    }
    uint8_t op = *p++;
    if ((op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF)) {  // string ops use esi / edi
        bool fixed = false;
        if (c->Esi < LOW_LIMIT) c->Esi += 0x80000000u, fixed = true;
        if (c->Edi < LOW_LIMIT) c->Edi += 0x80000000u, fixed = true;
        return fixed;
    }
    if (op == 0x0F) {
        uint8_t op2 = *p++;
        if (op2 == 0x38 || op2 == 0x3A) p++;
    } else if (!has_modrm_1byte(op)) {
        return false;
    }
    uint8_t modrm = *p++;
    int mod = modrm >> 6, rm = modrm & 7;
    if (mod == 3) return false;
    int base = rm, index = -1, scale = 0;
    if (rm == 4) {
        uint8_t sib = *p++;
        scale = sib >> 6;
        index = (sib >> 3) & 7;
        if (index == 4) index = -1;
        base = sib & 7;
        if (base == 5 && mod == 0) base = -1;
    } else if (rm == 5 && mod == 0) {
        return false;  // absolute low address: nothing to redirect
    }
    if (base >= 0 && *reg_ptr(c, base) < LOW_LIMIT) {
        *reg_ptr(c, base) += 0x80000000u;
        return true;
    }
    if (index >= 0 && scale == 0 && *reg_ptr(c, index) < LOW_LIMIT) {
        *reg_ptr(c, index) += 0x80000000u;
        return true;
    }
    return false;
}

// ---- data watchpoint (PARAPPA_WATCH=<hex address>) ----------------------------------------------
// A CPU debug register traps every write to a 4-byte word; the writer is logged with its stack.
DWORD g_watch_addr;

LONG on_watch(EXCEPTION_POINTERS *ep) {
    CONTEXT *c = ep->ContextRecord;
    if (!(c->Dr6 & 7)) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 = 0;
    static int hits;
    static uint32_t last;
    uint32_t v = *reinterpret_cast<const volatile uint32_t *>(uintptr_t(g_watch_addr));
    if (v != last && hits++ < 64) {  // only writes that change the value
        LOGW("Watch: %08lX = %08X written by %08lX (exe base %08X)", g_watch_addr, v, c->Eip,
             unsigned(uintptr_t(GetModuleHandleW(nullptr))));
        log_stack(c);
    }
    last = v;
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI arm_watch(void *thread) {
    HANDLE t = static_cast<HANDLE>(thread);
    SuspendThread(t);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(t, &c);
    // the same RAM word through its three views: KSEG0, KSEG1 and the KUSEG mirror
    uint32_t phys = g_watch_addr & 0x1FFFFCu;
    c.Dr0 = 0x80000000u | phys;
    c.Dr1 = 0xA0000000u | phys;
    c.Dr2 = phys;
    // L0-L2 enabled; each: write-only (01), 4 bytes (11)
    c.Dr7 = (c.Dr7 & ~0x0FFF003Fu) | 0x15u | (0xDu << 16) | (0xDu << 20) | (0xDu << 24);
    SetThreadContext(t, &c);
    ResumeThread(t);
    return 0;
}

LONG WINAPI on_low_access(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && g_watch_addr) return on_watch(ep);
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || er->NumberParameters < 2 ||
        er->ExceptionInformation[1] >= LOW_LIMIT)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD eip = ep->ContextRecord->Eip;
    if (!redirect_low_access(ep->ContextRecord)) return EXCEPTION_CONTINUE_SEARCH;
    static DWORD logged[64];
    static int nlogged;
    bool seen = false;
    for (int i = 0; i < nlogged; i++) seen |= logged[i] == eip;
    if (!seen && nlogged < 64) {
        logged[nlogged++] = eip;
        LOGD("Low-address access %08lX redirected at %08lX (exe base %08X)", (unsigned long)er->ExceptionInformation[1],
             eip, unsigned(uintptr_t(GetModuleHandleW(nullptr))));
        log_stack(ep->ContextRecord, psx::LogLevel::Debug);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

LONG WINAPI on_crash(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    const CONTEXT *ctx = ep->ContextRecord;
    uintptr_t base = uintptr_t(GetModuleHandleW(nullptr));
    LOGE("Crash: exception %08lX at %08lX (exe base %08X)", er->ExceptionCode, ctx->Eip, unsigned(base));
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        LOGE("  %s address %08lX", er->ExceptionInformation[0] ? "writing" : "reading",
             (unsigned long)er->ExceptionInformation[1]);
    LOGE("  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX", ctx->Eax, ctx->Ebx,
         ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
    log_stack(ctx);
    // The executable has no console: say what happened unless a scripted test run is going.
    if (!GetEnvironmentVariableA("PARAPPA_AUTOINPUT", nullptr, 0)) {
        MessageBoxA(nullptr,
                    "PaRappa the Rapper crashed.\n\nThe details are in parappa.log in %APPDATA%\\PaRappaPort.",
                    "PaRappa the Rapper", MB_OK | MB_ICONERROR);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace psx {

void install_crash_handler() {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    AddVectoredExceptionHandler(1, on_low_access);
    SetUnhandledExceptionFilter(on_crash);
}

// PARAPPA_WATCH=80049428: logs every write to that PS1 address (debugging memory corruption).
void install_watchpoint() {
    char buf[32];
    if (!GetEnvironmentVariableA("PARAPPA_WATCH", buf, sizeof(buf))) return;
    g_watch_addr = DWORD(strtoul(buf, nullptr, 16));
    if (!g_watch_addr) return;
    HANDLE h = CreateThread(nullptr, 0, arm_watch, g_main_thread, 0, nullptr);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    LOGI("Watching writes to %08lX", g_watch_addr);
}

// Logs where the main (game) thread currently is; used by the stall watchdog.
void log_main_thread() {
    if (!g_main_thread || SuspendThread(g_main_thread) == DWORD(-1)) return;
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(g_main_thread, &ctx)) {
        LOGW("Main thread at %08lX (exe base %08X)", ctx.Eip, unsigned(uintptr_t(GetModuleHandleW(nullptr))));
        log_stack(&ctx);
    }
    ResumeThread(g_main_thread);
}

}  // namespace psx
