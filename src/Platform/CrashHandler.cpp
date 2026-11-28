// Copyright (c) 2025, WH, All rights reserved.
#include "config.h"
#include "Timing.h"
#include "CrashHandler.h"
#include "Algorithms.h"

#ifdef MCENGINE_PLATFORM_WASM

namespace CrashHandler {
void init() {}
}  // namespace CrashHandler

#else

#include "BaseEnvironment.h"
#include "LaunchArgs.h"
#include "Logging.h"
#include "Paths.h"

#include "fmt/format.h"
#include "neotrace/neotrace.h"

#include <string>
#include <string_view>
#include <vector>

#ifdef MCENGINE_PLATFORM_WINDOWS

#include "UniString.h"
#include "dynutils.h"

#include "WinDebloatDefs.h"

#include <windows.h>
#include <winver.h>
#include <processthreadsapi.h>
#include <timezoneapi.h>
#include <sysinfoapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <dbghelp.h>
#include <errhandlingapi.h>
#include <libloaderapi.h>
#include <synchapi.h>

#include <array>

#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif

#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0x00000000L
#endif

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR) - 1)
#endif

#ifndef SEM_FAILCRITICALERRORS
#define SEM_FAILCRITICALERRORS 0x0001
#endif

#ifndef SEM_NOGPFAULTERRORBOX
#define SEM_NOGPFAULTERRORBOX 0x0002
#endif

#include <csignal>
#include <cstdlib>
#include <exception>

#ifdef _MSC_VER
#define RET_ADDR() _ReturnAddress()
#else
#define RET_ADDR() __builtin_return_address(0)
#endif

#else

#include <array>
#include <atomic>
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

#endif

namespace CrashHandler {

namespace {

std::string s_crashesDir;
std::string s_dir;
std::string s_tracePath;
std::vector<std::string> s_reporterArgs;
const void *s_exeBase{nullptr};
bool s_report{false};

#ifdef MCENGINE_PLATFORM_WINDOWS
using FileHandle = HANDLE;
#else
using FileHandle = int;
#endif

void put(FileHandle file, std::string_view text) {
#ifdef MCENGINE_PLATFORM_WINDOWS
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
#else
    (void)!write(file, text.data(), text.size());
#endif
}

// formats on the stack, since the heap may be broken
template <typename... Args>
void putf(FileHandle file, fmt::format_string<Args...> format, Args &&...args) {
    std::array<char, 256> buffer;
    const auto result = fmt::format_to_n(buffer.data(), buffer.size(), format, std::forward<Args>(args)...);
    put(file, {buffer.data(), static_cast<size_t>(result.out - buffer.data())});
}

// symbolizing can hang after a crash, so the raw addresses go first
void putAddresses(FileHandle file, const neotrace::trace &trace) {
    putf(file, "executable at {}\naddresses:", s_exeBase);
    for(const void *address : trace) putf(file, " {}", address);
    put(file, "\n\n");
}

void putSymbols(FileHandle file, const neotrace::trace &trace) {
    put(file, neotrace::to_string(trace));
    put(file, "\n");
}

#ifdef MCENGINE_PLATFORM_WINDOWS

dynutils::lib_obj *dbghelp_handle{nullptr};
using WINBOOL = int;
using MiniDumpWriteDump_t = WINBOOL WINAPI(HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType,
                                           CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                           CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                           CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
MiniDumpWriteDump_t *pMiniDumpWriteDump{nullptr};

std::array<char, 1024 + 1> g_userStream{};
std::wstring s_crashesDirW;
std::wstring s_dirW;
std::wstring s_dumpPathW;
std::wstring s_tracePathW;
std::wstring s_exeW;
std::wstring s_cmdlineW;
LONG g_dumpInProgress{0};
DWORD g_dumpingThread{0};
HANDLE g_dumpCompleteEvent{nullptr};
constexpr ULONG MCENGINE_USER_STREAM_TYPE = 0x4D430000;

bool alreadyDumping() {
    if(InterlockedCompareExchange(&g_dumpInProgress, 1, 0) == 0) {
        g_dumpingThread = GetCurrentThreadId();
        return false;
    }
    const bool handlerCrashed = g_dumpingThread == GetCurrentThreadId();
    if(handlerCrashed) {
        TerminateProcess(GetCurrentProcess(), 3);
    }
    return g_dumpCompleteEvent == nullptr || WaitForSingleObject(g_dumpCompleteEvent, INFINITE) == WAIT_OBJECT_0;
}

DWORD WINAPI watchdog(LPVOID finished) {
    constexpr DWORD handlerTimeoutMs = 30000;
    if(WaitForSingleObject(finished, handlerTimeoutMs) != WAIT_OBJECT_0) {
        TerminateProcess(GetCurrentProcess(), 3);
    }
    return 0;
}

LONG write_crash_dump_internal(EXCEPTION_POINTERS *exceptionInfo) {
    HANDLE finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CreateThread(nullptr, 0, watchdog, finished, 0, nullptr);

    const neotrace::trace trace = neotrace::trace::current();

    CreateDirectoryW(s_crashesDirW.c_str(), nullptr);
    CreateDirectoryW(s_dirW.c_str(), nullptr);

    const DWORD pid = GetCurrentProcessId();
    HANDLE hFile = pMiniDumpWriteDump ? CreateFileW(s_dumpPathW.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                                      : INVALID_HANDLE_VALUE;

    if(hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionParam{
            .ThreadId = GetCurrentThreadId(), .ExceptionPointers = exceptionInfo, .ClientPointers = FALSE};

        const auto dumpType =
            static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo);

        MINIDUMP_USER_STREAM userStream;
        MINIDUMP_USER_STREAM_INFORMATION userStreamInfo;

        if(g_userStream[0] != '\0') {
            userStream.Type = MCENGINE_USER_STREAM_TYPE;
            userStream.BufferSize = strlen(g_userStream.data());
            userStream.Buffer = g_userStream.data();

            userStreamInfo.UserStreamCount = 1;
            userStreamInfo.UserStreamArray = &userStream;
        } else {
            userStreamInfo.UserStreamCount = 0;
            userStreamInfo.UserStreamArray = nullptr;
        }

        WINBOOL success =
            pMiniDumpWriteDump(GetCurrentProcess(), pid, hFile, dumpType, &exceptionParam, &userStreamInfo, nullptr);
        (void)success;

        CloseHandle(hFile);
    }

    HANDLE traceFile = CreateFileW(s_tracePathW.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    putf(traceFile, "exception {:#010x} at {}\n", exceptionInfo->ExceptionRecord->ExceptionCode,
         exceptionInfo->ExceptionRecord->ExceptionAddress);
    if(g_userStream[0] != '\0') put(traceFile, g_userStream.data());
    putAddresses(traceFile, trace);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION reporter{};
    if(s_report && CreateProcessW(s_exeW.c_str(), s_cmdlineW.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                                  &startupInfo, &reporter)) {
        CloseHandle(reporter.hProcess);
        CloseHandle(reporter.hThread);
    }

    putSymbols(traceFile, trace);
    Logger::flush(true);
    CloseHandle(traceFile);
    SetEvent(finished);

    fflush(stdout);
    fflush(stderr);

    if(g_dumpCompleteEvent != nullptr) {
        SetEvent(g_dumpCompleteEvent);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI unhandled_exception_handler(EXCEPTION_POINTERS *exceptionInfo) {
    if(alreadyDumping()) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    return write_crash_dump_internal(exceptionInfo);
}

EXCEPTION_POINTERS g_exceptionPointers;
EXCEPTION_RECORD g_exceptionRecord;
CONTEXT g_context;

void abort_handler(int /*signal*/) {
    if(alreadyDumping()) {
        _exit(3);
    }

    memset(&g_context, 0, sizeof(CONTEXT));
    memset(&g_exceptionRecord, 0, sizeof(EXCEPTION_RECORD));

    RtlCaptureContext(&g_context);

    g_exceptionRecord.ExceptionCode = EXCEPTION_BREAKPOINT;
    g_exceptionRecord.ExceptionAddress = RET_ADDR();

    g_exceptionPointers.ContextRecord = &g_context;
    g_exceptionPointers.ExceptionRecord = &g_exceptionRecord;

    write_crash_dump_internal(&g_exceptionPointers);

    _exit(3);
}

void terminate_handler() { abort_handler(SIGABRT); }

void purecall_handler() {
    if(alreadyDumping()) {
        _exit(3);
    }

    memset(&g_context, 0, sizeof(CONTEXT));
    memset(&g_exceptionRecord, 0, sizeof(EXCEPTION_RECORD));

    RtlCaptureContext(&g_context);

    g_exceptionRecord.ExceptionCode = STATUS_NONCONTINUABLE_EXCEPTION;
    g_exceptionRecord.ExceptionAddress = RET_ADDR();

    g_exceptionPointers.ContextRecord = &g_context;
    g_exceptionPointers.ExceptionRecord = &g_exceptionRecord;

    write_crash_dump_internal(&g_exceptionPointers);

    _exit(3);
}

void invalid_parameter_handler(const wchar_t *expression, const wchar_t *function, const wchar_t *file,
                               unsigned int line, uintptr_t /*pReserved*/) {
    if(alreadyDumping()) {
        _exit(3);
    }

    memset(&g_context, 0, sizeof(CONTEXT));
    memset(&g_exceptionRecord, 0, sizeof(EXCEPTION_RECORD));

    RtlCaptureContext(&g_context);

    g_exceptionRecord.ExceptionCode = STATUS_INVALID_PARAMETER;
    g_exceptionRecord.ExceptionAddress = RET_ADDR();

    g_exceptionPointers.ContextRecord = &g_context;
    g_exceptionPointers.ExceptionRecord = &g_exceptionRecord;

    std::string errtext{"Invalid parameter detected:\n"};

    if(expression) {
        errtext += "  Expression: ";
        errtext += UniString::to_utf8(std::wstring_view{expression});
        errtext += '\n';
    }
    if(function) {
        errtext += "  Function: ";
        errtext += UniString::to_utf8(std::wstring_view{function});
        errtext += '\n';
    }
    if(file) {
        errtext += "  File: ";
        errtext += UniString::to_utf8(std::wstring_view{file});
        errtext += fmt::format(":{}\n", line);
    }

    memcpy(g_userStream.data(), errtext.c_str(), Mc::min(g_userStream.size() - 1, errtext.length()));

    write_crash_dump_internal(&g_exceptionPointers);

    _exit(3);
}

#else

std::vector<char *> s_reporterArgv;

const char *signalName(int sig) {
    switch(sig) {
        case SIGSEGV:
            return "SIGSEGV";
        case SIGBUS:
            return "SIGBUS";
        case SIGILL:
            return "SIGILL";
        case SIGFPE:
            return "SIGFPE";
        case SIGABRT:
            return "SIGABRT";
        case SIGTRAP:
            return "SIGTRAP";
        default:
            return "?";
    }
}

const void *crashedAt(const void *context) {
    [[maybe_unused]] const auto *uc = static_cast<const ucontext_t *>(context);
#if defined(MCENGINE_PLATFORM_MACOS) && defined(__aarch64__)
    return reinterpret_cast<const void *>(uc->uc_mcontext->__ss.__pc);
#elif defined(MCENGINE_PLATFORM_MACOS) && defined(__x86_64__)
    return reinterpret_cast<const void *>(uc->uc_mcontext->__ss.__rip);
#elif defined(MCENGINE_PLATFORM_LINUX) && defined(__x86_64__)
    return reinterpret_cast<const void *>(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(MCENGINE_PLATFORM_LINUX) && defined(__aarch64__)
    return reinterpret_cast<const void *>(uc->uc_mcontext.pc);
#else
    return nullptr;
#endif
}

void onSignal(int sig, siginfo_t *info, void *context) {
    static std::atomic_flag crashed;
    static pthread_t crashedThread;
    if(crashed.test_and_set()) {
        const bool handlerCrashed = pthread_equal(crashedThread, pthread_self());
        if(handlerCrashed) _exit(128 + sig);
        for(;;) pause();
    }
    crashedThread = pthread_self();

    constexpr unsigned handlerTimeoutSecs = 10;
    signal(SIGALRM, SIG_DFL);
    alarm(handlerTimeoutSecs);

    const neotrace::trace trace = neotrace::trace::current();

    mkdir(s_crashesDir.c_str(), 0755);
    mkdir(s_dir.c_str(), 0755);
    const int traceFile = open(s_tracePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    putf(traceFile, "signal {} ({}), code {}, address {}, at {}\n", sig, signalName(sig), info->si_code, info->si_addr,
         crashedAt(context));
    putAddresses(traceFile, trace);

    if(s_report) {
        pid_t reporter;
        posix_spawn(&reporter, s_reporterArgv[0], nullptr, nullptr, s_reporterArgv.data(), environ);
    }

    putSymbols(traceFile, trace);
    Logger::flush(true);
    close(traceFile);

#ifdef MCENGINE_PLATFORM_MACOS
    // re-raising would show the system crash dialog
    _exit(128 + sig);
#else
    // SA_RESETHAND restored the default action, so this ends the process with a core dump
    raise(sig);
#endif
}

#endif

}  // namespace

void init() {
    using Mc::LaunchArgs::has_arg;
    using enum Mc::LaunchArgs::ArgSwitch;

#ifdef MCENGINE_PLATFORM_WINDOWS
    const unsigned long pid = GetCurrentProcessId();
#else
    const long pid = getpid();
#endif
    s_crashesDir = Mc::Paths::data() + "/crashes";
    s_dir = fmt::format("{}/{}_{}", s_crashesDir, Timing::formatUTC("%Y-%m-%d_%H-%M-%S", Timing::getUnixTime()), pid);
    s_tracePath = s_dir + "/crash.txt";

    s_report = !Env::cfg(BUILD::DEBUG) && !has_arg(REND_HEADLESS);
    s_reporterArgs = {Mc::Paths::exe()};
    s_reporterArgs.insert(s_reporterArgs.end(), Mc::LaunchArgs::get_switches().begin(),
                          Mc::LaunchArgs::get_switches().end());
    s_reporterArgs.emplace_back("-crashreport");
    s_reporterArgs.push_back(s_dir);

    // the first capture allocates, so it happens now
    (void)neotrace::trace::current();

#ifdef MCENGINE_PLATFORM_WINDOWS
    s_exeBase = GetModuleHandleW(nullptr);
    s_crashesDirW = UniString::to_wide(s_crashesDir);
    s_dirW = UniString::to_wide(s_dir);
    s_dumpPathW = UniString::to_wide(s_dir + "/crash.dmp");
    s_tracePathW = UniString::to_wide(s_tracePath);
    s_exeW = UniString::to_wide(Mc::Paths::exe());
    for(const auto &arg : s_reporterArgs) {
        if(!s_cmdlineW.empty()) s_cmdlineW += L' ';
        s_cmdlineW += L'"' + UniString::to_wide(arg) + L'"';
    }

    {
        dbghelp_handle = dynutils::load_lib_system("dbghelp.dll");
        if(dbghelp_handle) {
            pMiniDumpWriteDump = dynutils::load_func<MiniDumpWriteDump_t>(dbghelp_handle, "MiniDumpWriteDump");
        }
        if(!pMiniDumpWriteDump || !dbghelp_handle) {
            debugLog("WARNING: can't generate crash dumps, could not load MiniDumpWriteDump: {}",
                     dynutils::get_error());
            dynutils::unload_lib(dbghelp_handle);
            pMiniDumpWriteDump = nullptr;
        }
    }

    g_dumpCompleteEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    SetUnhandledExceptionFilter(unhandled_exception_handler);

    // prevent CRT from removing our exception filter when abort() is called
#ifndef MCENGINE_PLATFORM_WINDOWS_MSVCRT
#ifndef _DEBUG
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    _set_abort_behavior(0, _CALL_REPORTFAULT);
#endif
#endif

    signal(SIGABRT, abort_handler);

    _set_purecall_handler(purecall_handler);
    _set_invalid_parameter_handler(invalid_parameter_handler);

    std::set_terminate(terminate_handler);

    {
        using SetThreadStackGuarantee_t = WINBOOL WINAPI(PULONG StackSizeInBytes);
        using SetErrorMode_t = UINT WINAPI(UINT uMode);

        SetThreadStackGuarantee_t *pset_thread_stack = nullptr;
        SetErrorMode_t *pset_error_mode = nullptr;

        const auto *kernel32_handle = reinterpret_cast<dynutils::lib_obj *>(GetModuleHandleA("kernel32.dll"));

        if(kernel32_handle) {
            pset_thread_stack =
                dynutils::load_func<SetThreadStackGuarantee_t>(kernel32_handle, "SetThreadStackGuarantee");
            pset_error_mode = dynutils::load_func<SetErrorMode_t>(kernel32_handle, "SetErrorMode");
        }

        if(pset_thread_stack) {
            ULONG handlerStackSize = 32UL * 1024;
            pset_thread_stack(&handlerStackSize);
        }

        if(pset_error_mode) {
            pset_error_mode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        }
    }
#else
    for(auto &arg : s_reporterArgs) s_reporterArgv.push_back(arg.data());
    s_reporterArgv.push_back(nullptr);

    if(Dl_info info; dladdr(reinterpret_cast<const void *>(&init), &info)) s_exeBase = info.dli_fbase;

    // lets the handler run after a stack overflow (main thread only)
    static std::array<char, 256 * 1024> altStack;
    stack_t stack{};
    stack.ss_sp = altStack.data();
    stack.ss_size = altStack.size();
    sigaltstack(&stack, nullptr);

    struct sigaction action{};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for(const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) sigaction(sig, &action, nullptr);
#endif
}

}  // namespace CrashHandler

#endif
