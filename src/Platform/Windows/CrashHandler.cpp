// Copyright (c) 2025, WH, All rights reserved.
#include "config.h"
#include "CrashHandler.h"

#ifndef MCENGINE_PLATFORM_WINDOWS  // does nothing on other platforms atm (linux creates coredumps already)

namespace CrashHandler {
void init() {}
}  // namespace CrashHandler

#else

#include "BaseEnvironment.h"
#include "UniString.h"
#include "dynutils.h"
#include "Logging.h"

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
#ifndef snwprintf
#define snwprintf _snwprintf
#endif
#define RET_ADDR() _ReturnAddress()
#else
#define RET_ADDR() __builtin_return_address(0)
#endif

namespace CrashHandler {

namespace {

static dynutils::lib_obj* dbghelp_handle{nullptr};
using WINBOOL = int;
using MiniDumpWriteDump_t = WINBOOL WINAPI(HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType,
                                           CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                           CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                           CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
static MiniDumpWriteDump_t* pMiniDumpWriteDump{nullptr};

static std::array<char, 1024 + 1> g_userStream{};
static std::array<wchar_t, 256 + 1> g_dumpFilenameW{};
static LONG g_dumpInProgress{0};
static HANDLE g_dumpCompleteEvent{nullptr};

#define ALREADY_DUMPING /* make sure only one thread writes the dump */                                       \
    /* the first thread is typically the one that caused the original crash; others are cascading failures */ \
    /* another thread is already writing the dump, wait for it to complete */                                 \
    (InterlockedCompareExchange(&g_dumpInProgress, 1, 0) != 0 &&                                              \
     (g_dumpCompleteEvent == nullptr ||                                                                       \
      WaitForSingleObject(g_dumpCompleteEvent, INFINITE) == 0x00000000L /* WAIT_OBJECT_0 */))

static LONG write_crash_dump_internal(EXCEPTION_POINTERS* exceptionInfo) {
    // generate filename with timestamp and PID for uniqueness
    const DWORD pid = GetCurrentProcessId();
    SYSTEMTIME time;
    GetLocalTime(&time);

    snwprintf(g_dumpFilenameW.data(), 256, L"crash_%hu-%hu-%hu_%hu-%hu-%hu_pid%lu.dmp", time.wYear, time.wMonth,
              time.wDay, time.wHour, time.wMinute, time.wSecond, pid);

    HANDLE hFile = CreateFileW(g_dumpFilenameW.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);

    if(hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionParam{
            .ThreadId = GetCurrentThreadId(), .ExceptionPointers = exceptionInfo, .ClientPointers = FALSE};

        const auto dumpType =
            static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo);

        MINIDUMP_USER_STREAM userStream;
        MINIDUMP_USER_STREAM_INFORMATION userStreamInfo;

        // include user stream data
        if(g_userStream[0] != '\0') {
            userStream.Type = 0x4D430000;  // "MC" in hex
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

    fflush(stdout);
    fflush(stderr);

    // signal other threads that dump is complete
    if(g_dumpCompleteEvent != nullptr) {
        SetEvent(g_dumpCompleteEvent);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI unhandled_exception_handler(EXCEPTION_POINTERS* exceptionInfo) {
    if(ALREADY_DUMPING) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // this thread won the race and is capturing its exception info (the root cause)
    return write_crash_dump_internal(exceptionInfo);
}

static EXCEPTION_POINTERS g_exceptionPointers;
static EXCEPTION_RECORD g_exceptionRecord;
static CONTEXT g_context;

static void abort_handler(int /*signal*/) {
    if(ALREADY_DUMPING) {
        _exit(3);
    }

    // this thread won the race, capture context to globals
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

static void terminate_handler() { abort_handler(SIGABRT); }

static void purecall_handler() {
    if(ALREADY_DUMPING) {
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

static void invalid_parameter_handler(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                                      unsigned int line, uintptr_t /*pReserved*/) {
    if(ALREADY_DUMPING) {
        _exit(3);
    }

    memset(&g_context, 0, sizeof(CONTEXT));
    memset(&g_exceptionRecord, 0, sizeof(EXCEPTION_RECORD));

    RtlCaptureContext(&g_context);

    g_exceptionRecord.ExceptionCode = STATUS_INVALID_PARAMETER;
    g_exceptionRecord.ExceptionAddress = RET_ADDR();

    g_exceptionPointers.ContextRecord = &g_context;
    g_exceptionPointers.ExceptionRecord = &g_exceptionRecord;

    // capture the invalid parameter information for the minidump
    std::array<wchar_t, 16 + 1> lineBuf{};

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
        snwprintf(lineBuf.data(), 16, L"%u", line);
        errtext += "  File: ";
        errtext += UniString::to_utf8(std::wstring_view{file});
        errtext += ':';
        errtext += UniString::to_utf8(std::wstring_view{lineBuf.data()});
        errtext += '\n';
    }

    memcpy(g_userStream.data(), errtext.c_str(), std::min(g_userStream.size() - 1, errtext.length()));

    write_crash_dump_internal(&g_exceptionPointers);

    _exit(3);
}

}  // namespace

void init() {
    // first, see if we can load MiniDumpWriteDump from dbghelp before doing anything
    {
        dbghelp_handle = dynutils::load_lib_system("dbghelp.dll");
        if(dbghelp_handle) {
            pMiniDumpWriteDump = dynutils::load_func<MiniDumpWriteDump_t>(dbghelp_handle, "MiniDumpWriteDump");
        }
        if(!pMiniDumpWriteDump || !dbghelp_handle) {
            debugLog("WARNING: can't generate crash dumps, could not load MiniDumpWriteDump: {}",
                     dynutils::get_error());
            dynutils::unload_lib(dbghelp_handle);
            return;  // no point in continuing
        }
    }

    // NOTE: the first event to hit the unhandled exception handler is probably the one that we actually care about for writing the dump
    // so ignore any other crashes from other threads during any unclean shutdown teardown
    g_dumpCompleteEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    SetUnhandledExceptionFilter(unhandled_exception_handler);

    // prevent CRT from removing our exception filter when abort() is called
#ifndef MCENGINE_PLATFORM_WINDOWS_MSVCRT
#ifndef _DEBUG
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    _set_abort_behavior(0, _CALL_REPORTFAULT);  // keep the message in debug builds
#endif
#endif

    signal(SIGABRT, abort_handler);

    _set_purecall_handler(purecall_handler);
    _set_invalid_parameter_handler(invalid_parameter_handler);

    std::set_terminate(terminate_handler);

    {
        using SetThreadStackGuarantee_t = WINBOOL WINAPI(PULONG StackSizeInBytes);
        using SetErrorMode_t = UINT WINAPI(UINT uMode);

        SetThreadStackGuarantee_t* pset_thread_stack = nullptr;
        SetErrorMode_t* pset_error_mode = nullptr;

        const auto* kernel32_handle = reinterpret_cast<dynutils::lib_obj*>(GetModuleHandleA("kernel32.dll"));

        if(kernel32_handle) {
            pset_thread_stack =
                dynutils::load_func<SetThreadStackGuarantee_t>(kernel32_handle, "SetThreadStackGuarantee");
            pset_error_mode = dynutils::load_func<SetErrorMode_t>(kernel32_handle, "SetErrorMode");
        }

        if(pset_thread_stack) {
            // reserve stack space for exception handlers (32KB should be enough for minidump creation)
            ULONG stackSizeInBytes = 32UL * 1024;
            pset_thread_stack(&stackSizeInBytes);
        }

        if(pset_error_mode) {
            // disable Windows Error Reporting BS
            pset_error_mode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        }
    }
}

}  // namespace CrashHandler

#endif
