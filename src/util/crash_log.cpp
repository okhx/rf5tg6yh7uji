#include "crash_log.hpp"
#include "paths.hpp"

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS

// Must come before DbgHelp so NOMINMAX / WIN32_LEAN_AND_MEAN are already set
#include <Windows.h>
#include <DbgHelp.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

// DbgHelp functions are loaded at runtime to avoid a hard-link dependency.
// They are available on every modern Windows installation.
using SymInitialize_t           = BOOL  (WINAPI*)(HANDLE, PCSTR, BOOL);
using SymCleanup_t              = BOOL  (WINAPI*)(HANDLE);
using SymSetOptions_t           = DWORD (WINAPI*)(DWORD);
using StackWalk64_t             = BOOL  (WINAPI*)(DWORD, HANDLE, HANDLE,
                                                   LPSTACKFRAME64, PVOID,
                                                   PREAD_PROCESS_MEMORY_ROUTINE64,
                                                   PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                                   PGET_MODULE_BASE_ROUTINE64,
                                                   PTRANSLATE_ADDRESS_ROUTINE64);
using SymFromAddr_t             = BOOL  (WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
using SymGetLineFromAddr64_t    = BOOL  (WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
using SymFunctionTableAccess64_t= PVOID (WINAPI*)(HANDLE, DWORD64);
using SymGetModuleBase64_t      = DWORD64(WINAPI*)(HANDLE, DWORD64);

namespace {

HMODULE             g_dbgHelpMod                = nullptr;
SymInitialize_t     g_SymInitialize             = nullptr;
SymCleanup_t        g_SymCleanup                = nullptr;
SymSetOptions_t     g_SymSetOptions             = nullptr;
StackWalk64_t       g_StackWalk64               = nullptr;
SymFromAddr_t       g_SymFromAddr               = nullptr;
SymGetLineFromAddr64_t     g_SymGetLineFromAddr64      = nullptr;
SymFunctionTableAccess64_t g_SymFunctionTableAccess64  = nullptr;
SymGetModuleBase64_t       g_SymGetModuleBase64        = nullptr;

void initDbgHelp() {
    if (g_dbgHelpMod) return;
    g_dbgHelpMod = LoadLibraryA("DbgHelp.dll");
    if (!g_dbgHelpMod) return;

    g_SymInitialize             = (SymInitialize_t)GetProcAddress(g_dbgHelpMod, "SymInitialize");
    g_SymCleanup                = (SymCleanup_t)GetProcAddress(g_dbgHelpMod, "SymCleanup");
    g_SymSetOptions             = (SymSetOptions_t)GetProcAddress(g_dbgHelpMod, "SymSetOptions");
    g_StackWalk64               = (StackWalk64_t)GetProcAddress(g_dbgHelpMod, "StackWalk64");
    g_SymFromAddr               = (SymFromAddr_t)GetProcAddress(g_dbgHelpMod, "SymFromAddr");
    g_SymGetLineFromAddr64      = (SymGetLineFromAddr64_t)GetProcAddress(g_dbgHelpMod, "SymGetLineFromAddr64");
    g_SymFunctionTableAccess64  = (SymFunctionTableAccess64_t)GetProcAddress(g_dbgHelpMod, "SymFunctionTableAccess64");
    g_SymGetModuleBase64        = (SymGetModuleBase64_t)GetProcAddress(g_dbgHelpMod, "SymGetModuleBase64");
}

std::filesystem::path makeLogPath() {
    auto dir = silicate::paths::directory("logs");
    
    // Build filename: crash_YYYYMMDD_HHMMSS.log
    std::time_t t  = std::time(nullptr);
    std::tm     tm = {};
    localtime_s(&tm, &t);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "crash_%04d%02d%02d_%02d%02d%02d.log",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return dir / buf;
}

// Exception code → human-readable string
static const char* exceptionName(DWORD code) {
    switch (code) {
#define CASE(x) case x: return #x
        CASE(EXCEPTION_ACCESS_VIOLATION);
        CASE(EXCEPTION_ARRAY_BOUNDS_EXCEEDED);
        CASE(EXCEPTION_BREAKPOINT);
        CASE(EXCEPTION_DATATYPE_MISALIGNMENT);
        CASE(EXCEPTION_FLT_DENORMAL_OPERAND);
        CASE(EXCEPTION_FLT_DIVIDE_BY_ZERO);
        CASE(EXCEPTION_FLT_INEXACT_RESULT);
        CASE(EXCEPTION_FLT_INVALID_OPERATION);
        CASE(EXCEPTION_FLT_OVERFLOW);
        CASE(EXCEPTION_FLT_STACK_CHECK);
        CASE(EXCEPTION_FLT_UNDERFLOW);
        CASE(EXCEPTION_ILLEGAL_INSTRUCTION);
        CASE(EXCEPTION_IN_PAGE_ERROR);
        CASE(EXCEPTION_INT_DIVIDE_BY_ZERO);
        CASE(EXCEPTION_INT_OVERFLOW);
        CASE(EXCEPTION_INVALID_DISPOSITION);
        CASE(EXCEPTION_NONCONTINUABLE_EXCEPTION);
        CASE(EXCEPTION_PRIV_INSTRUCTION);
        CASE(EXCEPTION_SINGLE_STEP);
        CASE(EXCEPTION_STACK_OVERFLOW);
#undef CASE
        default: return "UNKNOWN_EXCEPTION";
    }
}

// Write the full stack trace to the output stream using DbgHelp
void writeStackTrace(std::ofstream& out, CONTEXT* ctx) {
    HMODULE hDbgHelp = LoadLibraryA("DbgHelp.dll");
    if (!hDbgHelp) {
        out << "  [DbgHelp.dll could not be loaded]\n";
        return;
    }

#define LOAD(fn) auto fn##_ = reinterpret_cast<fn##_t>(GetProcAddress(hDbgHelp, #fn))
    LOAD(SymInitialize);
    LOAD(SymCleanup);
    LOAD(SymSetOptions);
    LOAD(StackWalk64);
    LOAD(SymFunctionTableAccess64);
    LOAD(SymGetModuleBase64);
    LOAD(SymFromAddr);
    LOAD(SymGetLineFromAddr64);
#undef LOAD

    if (!SymInitialize_ || !StackWalk64_ || !SymFromAddr_) {
        out << "  [DbgHelp symbols not available]\n";
        FreeLibrary(hDbgHelp);
        return;
    }

    HANDLE hProcess = GetCurrentProcess();
    HANDLE hThread  = GetCurrentThread();

    if (SymSetOptions_)
        SymSetOptions_(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);

    SymInitialize_(hProcess, nullptr, TRUE);

    STACKFRAME64 frame = {};
    frame.AddrPC.Mode    = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

#ifdef _WIN64
    constexpr DWORD machineType     = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx->Rip;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrStack.Offset = ctx->Rsp;
#else
    constexpr DWORD machineType     = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = ctx->Eip;
    frame.AddrFrame.Offset = ctx->Ebp;
    frame.AddrStack.Offset = ctx->Esp;
#endif

    // Symbol buffer
    constexpr ULONG kSymBufSize = sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR);
    char symBuf[kSymBufSize]    = {};
    auto* sym                   = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct            = sizeof(SYMBOL_INFO);
    sym->MaxNameLen              = MAX_SYM_NAME;

    int frameIndex = 0;
    while (StackWalk64_(machineType, hProcess, hThread, &frame, ctx,
                        nullptr,
                        SymFunctionTableAccess64_,
                        SymGetModuleBase64_,
                        nullptr)) {
        if (frame.AddrPC.Offset == 0) break;

        out << "  #" << frameIndex++ << "  0x"
            << std::hex << frame.AddrPC.Offset << std::dec << "  ";

        // Module name
        MEMORY_BASIC_INFORMATION mbi = {};
        char modName[MAX_PATH]       = "<unknown module>";
        if (VirtualQuery(reinterpret_cast<LPCVOID>(frame.AddrPC.Offset), &mbi, sizeof(mbi))) {
            GetModuleFileNameA(static_cast<HMODULE>(mbi.AllocationBase), modName, MAX_PATH);
        }
        // Strip to basename
        const char* base = std::strrchr(modName, '\\');
        out << (base ? base + 1 : modName) << "!";

        DWORD64 displacement = 0;
        if (SymFromAddr_(hProcess, frame.AddrPC.Offset, &displacement, sym)) {
            out << sym->Name << " + 0x" << std::hex << displacement << std::dec;
        } else {
            out << "?";
        }

        if (SymGetLineFromAddr64_) {
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct    = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisp       = 0;
            if (SymGetLineFromAddr64_(hProcess, frame.AddrPC.Offset, &lineDisp, &line)) {
                out << "  [" << line.FileName << ":" << line.LineNumber << "]";
            }
        }
        out << "\n";

        if (frameIndex > 64) { out << "  ... (truncated)\n"; break; }
    }

    if (SymCleanup_) SymCleanup_(hProcess);
    FreeLibrary(hDbgHelp);
}

void writeRegisters(std::ofstream& out, CONTEXT* ctx) {
#ifdef _WIN64
    out << std::hex;
    out << "  RAX=" << ctx->Rax << "  RBX=" << ctx->Rbx
        << "  RCX=" << ctx->Rcx << "  RDX=" << ctx->Rdx << "\n"
        << "  RSI=" << ctx->Rsi << "  RDI=" << ctx->Rdi
        << "  RSP=" << ctx->Rsp << "  RBP=" << ctx->Rbp << "\n"
        << "  R8 =" << ctx->R8  << "  R9 =" << ctx->R9
        << "  R10=" << ctx->R10 << "  R11=" << ctx->R11 << "\n"
        << "  R12=" << ctx->R12 << "  R13=" << ctx->R13
        << "  R14=" << ctx->R14 << "  R15=" << ctx->R15 << "\n"
        << "  RIP=" << ctx->Rip << "\n";
    out << std::dec;
#else
    out << std::hex;
    out << "  EAX=" << ctx->Eax << "  EBX=" << ctx->Ebx
        << "  ECX=" << ctx->Ecx << "  EDX=" << ctx->Edx << "\n"
        << "  ESI=" << ctx->Esi << "  EDI=" << ctx->Edi
        << "  ESP=" << ctx->Esp << "  EBP=" << ctx->Ebp << "\n"
        << "  EIP=" << ctx->Eip << "\n";
    out << std::dec;
#endif
}

// ──────────────────────────────────────────────────────────────────────────────
// Core log writer
// ──────────────────────────────────────────────────────────────────────────────

void writeCrashLog(EXCEPTION_POINTERS* ep, const char* reason = nullptr) {
    try {
        auto path = makeLogPath();
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) return;

        // Timestamp
        std::time_t t  = std::time(nullptr);
        std::tm     tm = {};
        localtime_s(&tm, &t);
        char tsBuf[64];
        std::snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02d %02d:%02d:%02d",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                      tm.tm_hour, tm.tm_min, tm.tm_sec);

        out << "========================================\n"
            << "  Grape Crash Log\n"
            << "  Time: " << tsBuf << "\n"
            << "========================================\n\n";

        if (reason) {
            out << "Reason: " << reason << "\n\n";
        }

        if (ep && ep->ExceptionRecord) {
            auto* er = ep->ExceptionRecord;
            out << "Exception: " << exceptionName(er->ExceptionCode)
                << " (0x" << std::hex << er->ExceptionCode << std::dec << ")\n"
                << "Address:   0x" << std::hex << reinterpret_cast<uintptr_t>(er->ExceptionAddress) << std::dec << "\n";

            if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
                out << "Access:    " << (er->ExceptionInformation[0] == 0 ? "READ" : "WRITE")
                    << " at 0x" << std::hex << er->ExceptionInformation[1] << std::dec << "\n";
            }
            out << "\n";
        }

        if (ep && ep->ContextRecord) {
            out << "Registers:\n";
            writeRegisters(out, ep->ContextRecord);
            out << "\nStack Trace:\n";
            writeStackTrace(out, ep->ContextRecord);
        } else {
            out << "Stack Trace: (no context available)\n";

            // Capture current context as best effort
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_FULL;
            RtlCaptureContext(&ctx);
            writeStackTrace(out, &ctx);
        }

        out << "\n========================================\n"
            << "  End of crash log\n"
            << "========================================\n";
        out.flush();

        // Also show a message box so the user knows where the log is
        auto msg = "Grape has crashed!\n\nLog saved to:\n" + path.string();
        MessageBoxA(nullptr, msg.c_str(), "Grape Crash", MB_OK | MB_ICONERROR);

    } catch (...) {
        // If logging itself crashes, there's nothing we can do
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Handlers
// ──────────────────────────────────────────────────────────────────────────────

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
    writeCrashLog(ep, "Unhandled SEH exception");
    return EXCEPTION_CONTINUE_SEARCH;
}

void terminateHandler() {
    writeCrashLog(nullptr, "std::terminate() called");
    // Call the old terminate handler if one was set, otherwise abort
    std::abort();
}

} // anonymous namespace

// ──────────────────────────────────────────────────────────────────────────────
// Public API
// ──────────────────────────────────────────────────────────────────────────────

void crash_log::install() {
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    std::set_terminate(terminateHandler);

    // Ensure logs directory exists right away (not lazily) so the path
    // is visible to the user before any crash occurs.
    silicate::paths::directory("logs");
}

#else

void crash_log::install() {}

#endif
