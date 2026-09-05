// Copyright (c) 2025-2026, WH, All rights reserved.
#include "Logging.h"

#include "Console.h"
#include "EngineConfig.h"
#include "SyncMutex.h"
#include "Thread.h"
#include "Environment.h"
#include "build_timestamp.h"

// for SDL_CleanupTLS
#include <SDL3/SDL_thread.h>

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "WinDebloatDefs.h"
#include <io.h>
#else
#include <unistd.h>
#endif

#include <array>

// TODO: handle log level switching at runtime
// we currently want all logging to be output, so set it to the most verbose level
// otherwise, the SPDLOG_ macros below SPD_LOG_LEVEL_INFO will just do (void)0;
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "spdlog/common.h"
#include "spdlog/async_logger.h"

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/pattern_formatter.h"

#define DEFAULT_LOGGER_NAME "main"
#define RAW_LOGGER_NAME "raw"
#define NETWORK_LOGGER_NAME "network"

#ifdef _DEBUG
// debug pattern: [filename:line] [function]: message
#define FANCY_LOG_PATTERN "[%s:%#] [%!]: %v"
// for file output, add timestamp (and thread, for debug) info
#define FILE_LOG_PATTERN_PREF "[%T.%e] [th:%t]"
#define RELEASE_IDENTIFIER "dev"
#else
// release pattern: [function] message
#define FANCY_LOG_PATTERN "[%!] %v"
// add HH:MM:SS timestamp
#define FILE_LOG_PATTERN_PREF "[%T]"
#define RELEASE_IDENTIFIER "rel"
#endif

// same as release build stdout logging, don't clutter engine logs with source info and stuff
#define ENGINE_CONSOLE_LOG_PATTERN "[%!] %v"

// e.g. ./logs/
#define LOGFILE_LOCATION MCENGINE_DATA_DIR "logs/"
// e.g. ./logs/neomod-linux-x64-dev-40.03.log
#define _LOGFILE_BASENAME LOGFILE_LOCATION PACKAGE_NAME "-" OS_NAME "-" RELEASE_IDENTIFIER "-" PACKAGE_VERSION_UNCACHED
#define LOGFILE_NAME _LOGFILE_BASENAME ".log"
#define LOGFILE_NAME_NETWORK _LOGFILE_BASENAME "-network.log"

namespace Logger {
namespace {  // static
std::shared_ptr<spdlog::async_logger> s_logger;
spdlog::async_logger *s_logger_raw_ptr{nullptr};
std::shared_ptr<spdlog::async_logger> s_raw_logger;
spdlog::async_logger *s_raw_logger_raw_ptr{nullptr};

// network channel (file-only, single format)
std::shared_ptr<spdlog::async_logger> s_network_logger;
spdlog::async_logger *s_network_logger_raw_ptr{nullptr};

bool s_log_initialized{false};

#ifdef MCENGINE_PLATFORM_WINDOWS
bool s_created_console{false};
#endif

// workaround for really odd internal template decisions in spdlog...
struct custom_spdmtx : public Sync::mutex {
    using mutex_t = Sync::mutex;
    static mutex_t &mutex() {
        static mutex_t s_mutex;
        return s_mutex;
    }
};

// custom %! (function name) formatter: std::source_location::current().function_name() gives the full
// signature (return type + parameters), and every compiler spells namespaces/lambdas differently, e.g.
// for a lambda inside an anonymous-namespace function "BANCHO::Net::attempt_logging_in":
//   clang: "auto BANCHO::Net::(anonymous namespace)::attempt_logging_in()::(lambda)::operator()(...) const"
//   gcc:   "BANCHO::Net::{anonymous}::attempt_logging_in()::<lambda()>"
//   msvc:  uses `anonymous namespace' and <lambda_N>::operator ()
// this reduces all of them to "BANCHO::Net::attempt_logging_in::λ" (or just "attempt_logging_in::λ" in release).
class custom_srcloc_formatter : public spdlog::custom_flag_formatter {
    // how a lambda's closure type is rendered (compilers spell it (lambda)/<lambda(...)>/<lambda_N>)
    static constexpr std::string_view LAMBDA_TOKEN{"λ"};

    static forceinline bool is_ident(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
    // bracket-like delimiters, so spaces and "::" nested inside (), <>, [], {} or msvc's `...' quoting
    // aren't treated as top-level separators
    static forceinline bool is_open(char c) { return c == '(' || c == '<' || c == '[' || c == '{' || c == '`'; }
    static forceinline bool is_close(char c) { return c == ')' || c == '>' || c == ']' || c == '}' || c == '\''; }

    static forceinline bool is_anon_ns(std::string_view seg) {
        return seg == "(anonymous namespace)" || seg == "{anonymous}" || seg == "`anonymous namespace'";
    }
    static forceinline bool is_lambda(std::string_view seg) {
        return seg.starts_with("(lambda") || seg.starts_with("<lambda");
    }

    static void append_clean(std::string_view fn, spdlog::memory_buf_t &dest) {
        const size_t n = fn.size();

        // 1) find the function's own parameter list: the first top-level '(' whose matching ')' is followed
        //    by neither "::" (a scope group like "(anonymous namespace)"/"(lambda)"/"foo()") nor '(' (the
        //    empty "()" of an operator()). everything from there on is parameters/qualifiers, so drop it.
        size_t name_end = n;
        {
            size_t depth = 0, cand = std::string_view::npos;
            for(size_t i = 0; i < n; ++i) {
                const char c = fn[i];
                if(c == '(') {
                    if(depth == 0) cand = i;
                    ++depth;
                } else if(is_open(c)) {
                    ++depth;
                } else if(c == ')') {
                    if(depth > 0) --depth;
                    if(depth == 0 && cand != std::string_view::npos) {
                        const size_t k = i + 1;
                        // followed by "::" (a scope group) or '(' (operator()'s "()") -> keep looking,
                        // otherwise this is the real parameter list
                        if((k + 1 < n && fn[k] == ':' && fn[k + 1] == ':') || (k < n && fn[k] == '('))
                            cand = std::string_view::npos;
                        else {
                            name_end = cand;
                            break;
                        }
                    }
                } else if(is_close(c)) {
                    if(depth > 0) --depth;
                }
            }
        }

        // 2) skip the return type / specifiers / calling convention: the qualified name begins after the
        //    last top-level space before it. stop at a top-level "operator" token so its own spaces
        //    (e.g. msvc "operator ()", "operator new") aren't mistaken for the return-type boundary.
        size_t name_begin = 0;
        {
            size_t depth = 0;
            for(size_t i = 0; i < name_end; ++i) {
                const char c = fn[i];
                if(is_open(c))
                    ++depth;
                else if(is_close(c)) {
                    if(depth > 0) --depth;
                } else if(depth == 0) {
                    if(c == ' ')
                        name_begin = i + 1;
                    else if(c == 'o' && fn.substr(i).starts_with("operator") && (i == 0 || !is_ident(fn[i - 1])) &&
                            (i + 8 >= name_end || !is_ident(fn[i + 8])))
                        break;
                }
            }
        }

        // 3) split the qualified name on top-level "::", normalising each segment into a small list
        struct Seg {
            std::string_view text;
            bool lambda;
        };
        std::array<Seg, 32> segs{};  // 32 is far more than any realistic namespace/class/lambda nesting
        size_t nsegs = 0;
        bool prev_lambda = false;

        const auto push = [&](std::string_view seg) {
            while(!seg.empty() && (seg.front() == ' ' || seg.front() == '*' || seg.front() == '&'))
                seg.remove_prefix(1);
            while(!seg.empty() && seg.back() == ' ') seg.remove_suffix(1);
            if(seg.empty() || is_anon_ns(seg)) return;  // drop file-local anonymous-namespace noise

            if(is_lambda(seg)) {
                if(nsegs < segs.size()) segs[nsegs++] = {LAMBDA_TOKEN, true};
                prev_lambda = true;
                return;
            }
            // a lambda's call operator immediately after its closure type is redundant once λ is printed
            if(prev_lambda && seg.starts_with("operator")) return;
            prev_lambda = false;

            // drop a trailing "(...)" shown on enclosing functions in lambda contexts (e.g. "foo()" -> "foo")
            if(!seg.starts_with("operator") && seg.back() == ')') {
                int d = 0;
                for(size_t i = seg.size(); i-- > 0;) {
                    if(seg[i] == ')')
                        ++d;
                    else if(seg[i] == '(' && --d == 0) {
                        seg.remove_suffix(seg.size() - i);
                        break;
                    }
                }
            }
            if(!seg.empty() && nsegs < segs.size()) segs[nsegs++] = {seg, false};
        };

        size_t seg_start = name_begin;
        {
            size_t depth = 0;
            for(size_t i = name_begin; i < name_end; ++i) {
                const char c = fn[i];
                if(is_open(c))
                    ++depth;
                else if(is_close(c)) {
                    if(depth > 0) --depth;
                } else if(depth == 0 && c == ':' && i + 1 < name_end && fn[i + 1] == ':') {
                    push(fn.substr(seg_start, i - seg_start));
                    seg_start = i + 2;
                    ++i;
                }
            }
        }
        push(fn.substr(seg_start, name_end - seg_start));

        if(nsegs == 0) return;

        size_t first = 0;
#ifndef _DEBUG
        // release pattern is terse: keep only the function name plus any trailing lambda markers
        for(first = nsegs - 1; first > 0 && segs[first].lambda;) --first;
#endif
        for(size_t i = first; i < nsegs; ++i) {
            if(i > first) dest.append(std::string_view{"::"});
            dest.append(segs[i].text);
        }
    }

   public:
    void format(const spdlog::details::log_msg &logmsg, const std::tm & /*tms*/, spdlog::memory_buf_t &dest) override {
        if(logmsg.source.funcname && logmsg.source.funcname[0] != '\0') append_clean(logmsg.source.funcname, dest);
    }

    [[nodiscard]] std::unique_ptr<custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<custom_srcloc_formatter>();
    }
};

}  // namespace

namespace _detail {

void log_int(uint8_t channel, std::source_location loc, log_level::level_enum lvl, std::string_view str) noexcept {
    // checking for wasInit for the unlikely case that we try to log something through here WHILE initializing/uninitializing
    if(likely(s_log_initialized)) {
        const auto spd_lvl = (spdlog::level::level_enum)lvl;
        const spdlog::source_loc spd_loc{loc.file_name(), static_cast<int>(loc.line()), loc.function_name()};
        if(channel & CHAN_DEFAULT) {
            s_logger_raw_ptr->log(spd_loc, spd_lvl, str);
        }
        if((channel & CHAN_NETWORK) && s_network_logger_raw_ptr) {
            s_network_logger_raw_ptr->log(spd_lvl, str);
        }
    } else {
        printf("%.*s\n", static_cast<int>(str.length()), str.data());
    }
}

void logRaw_int(uint8_t channel, log_level::level_enum lvl, std::string_view str) noexcept {
    if(likely(s_log_initialized)) {
        const auto spd_lvl = (spdlog::level::level_enum)lvl;
        if(channel & CHAN_DEFAULT) {
            s_raw_logger_raw_ptr->log(spd_lvl, str);
        }
        if((channel & CHAN_NETWORK) && s_network_logger_raw_ptr) {
            s_network_logger_raw_ptr->log(spd_lvl, str);
        }
    } else {
        printf("%.*s\n", static_cast<int>(str.length()), str.data());
    }
}

void logFmt_int(uint8_t channel, std::source_location loc, log_level::level_enum lvl, fmt::string_view fmt,
                fmt::format_args args) noexcept {
    fmt::memory_buffer buf;
    fmt::vformat_to(fmt::appender(buf), fmt, args);
    log_int(channel, loc, lvl, std::string_view{buf.data(), buf.size()});
}

void logRawFmt_int(uint8_t channel, log_level::level_enum lvl, fmt::string_view fmt, fmt::format_args args) noexcept {
    fmt::memory_buffer buf;
    fmt::vformat_to(fmt::appender(buf), fmt, args);
    logRaw_int(channel, lvl, std::string_view{buf.data(), buf.size()});
}

}  // namespace _detail
using namespace _detail;

namespace {  // static
// implementation of ConsoleSink
class ConsoleSink final : public spdlog::sinks::base_sink<custom_spdmtx> {
   private:
    // separate formatters for different logger types
    // the base class has a "formatter_" member which we can use as the main formatter
    std::unique_ptr<spdlog::pattern_formatter> raw_formatter_{nullptr};

   public:
    ConsoleSink() noexcept {
        // create separate formatters for different logger types
        // also, don't auto-append newlines, each console log is already on a new line
        auto tempformatter = std::make_unique<spdlog::pattern_formatter>(spdlog::pattern_time_type::local, "");
        tempformatter->add_flag<custom_srcloc_formatter>('!').set_pattern(ENGINE_CONSOLE_LOG_PATTERN);
        base_sink::formatter_ = std::move(tempformatter);

        // raw formatter always uses plain pattern
        raw_formatter_ = std::make_unique<spdlog::pattern_formatter>("%v", spdlog::pattern_time_type::local, "");
    }

   protected:
    // every line goes straight to the engine console (Console::log is thread-safe, it only queues the line)
    // TODO: implement color
    inline void sink_it_(const spdlog::details::log_msg &msg) noexcept override {
        spdlog::memory_buf_t formatted;

        // choose formatter based on logger name
        static_assert(RAW_LOGGER_NAME[0] == 'r');
        if(msg.logger_name[0] == RAW_LOGGER_NAME[0]) {  // raw
            raw_formatter_->format(msg, formatted);
        } else {  // cooked
            base_sink::formatter_->format(msg, formatted);
        }

        // the formatter doesn't append newlines, but the engine console doesn't like having them, so doing this just in case
        auto end_pos{formatted.size()};
        while(end_pos > 0 && (formatted[end_pos - 1] == '\r' || formatted[end_pos - 1] == '\n')) {
            --end_pos;
        }

        Console::log(std::string_view{formatted.data(), end_pos});
    }
    inline void flush_() noexcept override {}
};

// with basic_file_sink, it seems that multiple different sinks to the same file aren't properly synchronized (on Linux at least)
// so the pattern of using 2 loggers and 2 sinks doesn't quite work
// not a huge issue, we can just do a similar thing to ConsoleSink to use a different formatter based on the logger name,
// and register it with both loggers

// annoyingly, though, basic_file_sink is marked final, so it can't be overridden (we just have to reimplement it entirely :/)
class DualPatternFileSink final : public spdlog::sinks::base_sink<custom_spdmtx> {
   private:
    spdlog::details::file_helper file_helper_;

    // separate formatters for different logger types (same as consolebox)
    std::unique_ptr<spdlog::pattern_formatter> raw_formatter_{nullptr};

   public:
    explicit DualPatternFileSink(const spdlog::filename_t &filename, bool truncate = false,
                                 const spdlog::file_event_handlers &event_handlers = {}) noexcept
        : file_helper_{event_handlers} {
        // do both the prefix and the fancy log pattern
        auto tempformatter = std::make_unique<spdlog::pattern_formatter>();
        tempformatter->add_flag<custom_srcloc_formatter>('!').set_pattern(FILE_LOG_PATTERN_PREF " " FANCY_LOG_PATTERN);
        base_sink::formatter_ = std::move(tempformatter);

        // plain after the prefix
        raw_formatter_ = std::make_unique<spdlog::pattern_formatter>(FILE_LOG_PATTERN_PREF " %v");

        file_helper_.open(filename, truncate);
    }

    // the rest of this implementation is basically copied from spdlog's basic_file_sink

    [[nodiscard]] inline const spdlog::filename_t &filename() const noexcept { return file_helper_.filename(); }

    inline void truncate() {
        Sync::lock_guard lock(base_sink::mutex_);
        file_helper_.reopen(true);
    }

   protected:
    inline void sink_it_(const spdlog::details::log_msg &msg) noexcept override {
        spdlog::memory_buf_t formatted;

        static_assert(RAW_LOGGER_NAME[0] == 'r');
        if(msg.logger_name[0] == RAW_LOGGER_NAME[0]) {  // raw
            raw_formatter_->format(msg, formatted);
        } else {  // cooked
            base_sink::formatter_->format(msg, formatted);
        }

        file_helper_.write(formatted);
    }

    inline void flush_() override { file_helper_.flush(); }
};

}  // namespace

// to be called in main(), for one-time setup/teardown
void init(bool create_console) noexcept {
    if(s_log_initialized) return;

#ifdef MCENGINE_PLATFORM_WINDOWS
    // when the spdlog::sinks::wincolor_stdout_sink is created, it checks GetStdHandle(STD_OUTPUT_HANDLE) at initialization
    // so, create a console, such that GetStdHandle(STD_OUTPUT_HANDLE) returns a handle to it
    // this might be desirable on release builds, which are linked against the "windows" subsystem
    // (which don't create a console when opening the app)
    if(create_console && isatty(fileno(stdout)) == 0 /* don't create console if we're already in one */) {
        // allocate a new console window
        if((s_created_console = AllocConsole())) {
            // redirect stdout/stderr to the new console
            // using freopen is the simplest approach that works with both C and C++ streams
            FILE *fp = nullptr;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);

            SetConsoleTitleW(L"" PACKAGE_NAME L" " PACKAGE_VERSION_UNCACHED L" console output");

            SetConsoleOutputCP(65001 /*CP_UTF8*/);
        }
    }
#else
    (void)create_console;  // it's not as big of a commotion on platforms outside of windows
#endif

    // make console output visible immediately
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // initialize async thread pool before creating any async loggers
    // queue size: 32768 slots (each ~256 bytes), 1 background thread
    // use overrun_oldest policy for non-blocking behavior
    spdlog::init_thread_pool(
        32768, 1,
        []() -> void {  // pre-thread-init
            McThread::set_current_thread_name("spd_logger");
            McThread::set_current_thread_prio(McThread::Priority::LOW);
        },
        []() -> void {  // post-thread-shutdown cleanup
            // since spdlog creates std::threads,
            // need to clean up any SDL things from SDL functions that we might have
            // called from inside of it (including McThread::set_current_thread_prio)
            SDL_CleanupTLS();
        });

    // engine console sink handles its own formatting
    auto engine_console_sink{std::make_shared<ConsoleSink>()};

    // default debugLog sink
#ifdef MCENGINE_PLATFORM_WINDOWS
    using mt_stdout_sink_t = spdlog::sinks::wincolor_stdout_sink<custom_spdmtx>;
#else
    using mt_stdout_sink_t = spdlog::sinks::ansicolor_stdout_sink<custom_spdmtx>;
#endif
    auto stdout_sink{std::make_shared<mt_stdout_sink_t>()};
    {
        auto tempformatter = std::make_unique<spdlog::pattern_formatter>();
        tempformatter->add_flag<custom_srcloc_formatter>('!').set_pattern(FANCY_LOG_PATTERN);
        stdout_sink->set_formatter(std::move(tempformatter));
    }

    // unformatted stdout sink
    auto raw_stdout_sink{std::make_shared<mt_stdout_sink_t>()};
    raw_stdout_sink->set_pattern("%v");  // just the message

    // prepare sink lists for loggers
    std::vector<spdlog::sink_ptr> main_sinks{std::move(stdout_sink), engine_console_sink};
    std::vector<spdlog::sink_ptr> raw_sinks{std::move(raw_stdout_sink), engine_console_sink};

    // if Environment::createDirectory failed, it means we definitely won't be able to write to anything in there
    // (returns true if it already exists)
    const bool log_to_file{Environment::createDirectory(LOGFILE_LOCATION)};

    // add file sinks if directory is writable
    if(log_to_file) {
        // similar to the ConsoleSink, the logger source determines the output pattern
        auto file_sink{std::make_shared<DualPatternFileSink>(LOGFILE_NAME, true /* overwrite */)};

        main_sinks.push_back(file_sink);
        raw_sinks.push_back(file_sink);

        // network channel: file-only, single plain format
        auto network_file_sink{
            std::make_shared<spdlog::sinks::basic_file_sink<custom_spdmtx>>(LOGFILE_NAME_NETWORK, true)};
        network_file_sink->set_pattern("[%T] %v");  // just timestamp and message

        s_network_logger = std::make_shared<spdlog::async_logger>(
            NETWORK_LOGGER_NAME, spdlog::sinks_init_list{network_file_sink}, spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest);
        s_network_logger_raw_ptr = s_network_logger.get();
        s_network_logger->set_level(spdlog::level::trace);
        s_network_logger->flush_on(spdlog::level::off);
        spdlog::register_logger(s_network_logger);
    }

    // create main async logger with stdout + console + optional file sink
    s_logger =
        std::make_shared<spdlog::async_logger>(DEFAULT_LOGGER_NAME, main_sinks.begin(), main_sinks.end(),
                                               spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
    s_logger_raw_ptr = s_logger.get();

    // create raw async logger with separate stdout + console + optional file sink
    s_raw_logger =
        std::make_shared<spdlog::async_logger>(RAW_LOGGER_NAME, raw_sinks.begin(), raw_sinks.end(),
                                               spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
    s_raw_logger_raw_ptr = s_raw_logger.get();

    // set to trace level so we print out all messages
    // TODO: add custom log level support (ConVar callback + build type?)
    s_logger->set_level(spdlog::level::trace);
    s_raw_logger->set_level(spdlog::level::trace);

    // for async loggers, flush operations are queued to the background thread
    // disable automatic flushing based on level, let periodic flusher handle it
    s_logger->flush_on(spdlog::level::off);
    s_raw_logger->flush_on(spdlog::level::off);

    // register both loggers so they both get periodic flushing
    spdlog::register_logger(s_logger);
    spdlog::register_logger(s_raw_logger);

    // flush every 500ms
    // console commands will trigger a flush for responsiveness, though
    spdlog::flush_every(std::chrono::milliseconds(500));

    // make the s_logger default (doesn't really matter right now since we're handling it manually, i think, but still)
    spdlog::set_default_logger(s_logger);

    s_log_initialized = true;
};

// spdlog::shutdown() explodes if its called at program exit (by global atexit handler), so we need to manually shut it down
void shutdown() noexcept {
    if(!s_log_initialized) return;
    flush();
    s_log_initialized = false;

    s_network_logger_raw_ptr = nullptr;
    s_network_logger.reset();
    s_raw_logger_raw_ptr = nullptr;
    s_raw_logger.reset();
    s_logger_raw_ptr = nullptr;
    s_logger.reset();

    // spdlog docs recommend calling this on exit
    // for async loggers, this waits for the background thread to finish processing queued messages
    spdlog::shutdown();

#ifdef MCENGINE_PLATFORM_WINDOWS
    if(s_created_console) {
        FreeConsole();
        s_created_console = false;
    }
#endif
}

// manual trigger for console commands
void flush() noexcept {
    if(likely(s_log_initialized)) {
        s_logger->flush();
        s_raw_logger->flush();
        if(s_network_logger) s_network_logger->flush();
    } else {
        fflush(stdout);
        fflush(stderr);
    }
}

// extra util functions
bool isaTTY() noexcept {
    static const bool tty_status{isatty(fileno(stdout)) != 0};
    return tty_status;
}
}  // namespace Logger
