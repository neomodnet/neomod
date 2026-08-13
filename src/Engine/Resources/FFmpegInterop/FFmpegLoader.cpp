// Copyright (c) 2025-2026, WH, All rights reserved.
#include "config.h"

#if defined(MCENGINE_FEATURE_FFMPEG)

#include "FFmpegLoader.h"
#include "EngineConfig.h"
#include "dynutils.h"
#include "SyncMutex.h"
#include "SyncOnce.h"
#include "Logging.h"
#include "SString.h"

#include "ConVar.h"

namespace Mc::FFmpeg {
namespace funcs {
// generate function pointer definitions
#define DEFINE_FFMPEG_FUNCTION(name) name##_t *name{nullptr};  // NOLINT(bugprone-macro-parentheses)
ALL_FFMPEG_FUNCTIONS(DEFINE_FFMPEG_FUNCTION)
}  // namespace funcs
using namespace funcs;

namespace {  // anon

struct LoadContext {
    NOCOPY_NOMOVE(LoadContext)
   public:
    LoadContext() = default;
    ~LoadContext() { cleanup_internal(); }

    void cleanup_internal() {
#define RESET_LIB(libname)                      \
    if(!!(libname##_handle)) {                  \
        dynutils::unload_lib(libname##_handle); \
        libname##_handle = nullptr;             \
    }
        RESET_LIB(avformat);
        RESET_LIB(avcodec);
        RESET_LIB(swscale);
        RESET_LIB(avutil);
#undef RESET_LIB

// set function pointers back to null
#define RESET_FUNCTION(name) name = nullptr;
        ALL_FFMPEG_FUNCTIONS(RESET_FUNCTION)
#undef RESET_FUNCTION
    }

    // library handles
    dynutils::lib_obj *avutil_handle{nullptr};
    dynutils::lib_obj *swscale_handle{nullptr};
    dynutils::lib_obj *avcodec_handle{nullptr};
    dynutils::lib_obj *avformat_handle{nullptr};

    // initialization state
    std::string error_string{""};
};

LoadContext &ld_ctx() {
    static LoadContext ctx;
    return ctx;
}

int parse_log_level_from_string(std::string_view str) {
    constexpr bool is_debug = Env::cfg(BUILD::DEBUG);

    // default for debug builds, 0 (effectively disabled besides crashes) otherwise
    const int default_level = is_debug ? av_log_get_level() : 0;

    const std::string lowerstr = SString::to_lower(str);
    if(lowerstr.empty() || lowerstr == "0" || lowerstr == "false" || lowerstr == "none") return AV_LOG_QUIET;

    // TODO: check if this down-shifting makes any sense in practice
    if(lowerstr == "max") return AV_LOG_TRACE;
    if(lowerstr == "trace") return is_debug ? AV_LOG_TRACE : AV_LOG_DEBUG;
    if(lowerstr == "debug") return is_debug ? AV_LOG_DEBUG : AV_LOG_VERBOSE;
    if(lowerstr == "verbose") return is_debug ? AV_LOG_VERBOSE : AV_LOG_INFO;
    if(lowerstr == "info") return is_debug ? AV_LOG_INFO : AV_LOG_WARNING;
    if(lowerstr == "warn") return is_debug ? AV_LOG_WARNING : AV_LOG_ERROR;
    if(lowerstr == "error") return is_debug ? AV_LOG_ERROR : AV_LOG_FATAL;
    if(lowerstr == "fatal") return AV_LOG_FATAL;

    // unknown
    return default_level;
}
}  // namespace
}  // namespace Mc::FFmpeg

// awkwardly sandwiched in here to avoid needing to forward declare it
namespace cv {
ConVar debug_ffmpeg("debug_ffmpeg", "fatal", CLIENT,
                    "(0/empty/false/none);fatal;error;warn;info;verbose;debug;trace;max", [](std::string_view value) {
                        if(Mc::FFmpeg::funcs::av_log_set_level) {
                            Mc::FFmpeg::funcs::av_log_set_level(Mc::FFmpeg::parse_log_level_from_string(value));
                        }
                    });
}  // namespace cv

namespace Mc::FFmpeg {
namespace {
// The log callback must be thread-safe, according to the ffmpeg docs
Sync::mutex ff_log_mutex;
int ff_log_print_prefix{0};  // this makes no sense but is required

void ff_log_callback(void *avcl, int level, const char *fmt, va_list vl) {
    if(level >= 0) level &= 0xff;  // mask off "tint"

    if(level > av_log_get_level()) return;

    std::array<char, 1024> log_line{};
    Sync::scoped_lock lk(ff_log_mutex);

    int written = av_log_format_line2(avcl, level, fmt, vl, log_line.data(), static_cast<int>(log_line.size()),
                                      &ff_log_print_prefix);
    if(written <= 0) return;

    // sanitize control characters that could mess with terminal
    for(char *p = log_line.data(); *p; ++p) {
        auto c = static_cast<unsigned char>(*p);
        if(c < 0x08 || (c > 0x0D && c < 0x20)) *p = '?';
    }

    log_line.back() = '\0';
    std::string_view logstr{log_line.data()};
    while(!logstr.empty() && (logstr.back() == '\n' || logstr.back() == '\r')) {
        logstr.remove_suffix(1);
    }

    if(!logstr.empty()) {
        logRaw("FFmpeg: {:s}", logstr);
    }
}

struct FFmpegFuncset {
    std::string bare_libname;           // e.g. avformat
    dynutils::lib_obj **libhandle_ref;  // e.g. &avformat_handle
    int libversion;                     // e.g. 61
};

#ifdef MCENGINE_PLATFORM_WINDOWS
#define LNAMESTR(x, ver) fmt::format("{:s}-{:d}.dll", x, ver)
#elif defined(MCENGINE_PLATFORM_MACOS)
#define LNAMESTR(x, ver) fmt::format("lib{:s}.{:d}.dylib", x, ver)
#else
#define LNAMESTR(x, ver) fmt::format("lib{:s}.so.{:d}", x, ver)
#endif

bool load_full_ff_lib(const std::array<FFmpegFuncset, 4> &ffmpeg_funcsets) {
    for(auto &funcset : ffmpeg_funcsets) {
        const std::string trypath2 = LNAMESTR(funcset.bare_libname, funcset.libversion);
        const std::string trypath1 = fmt::format(MCENGINE_LIB_PATH "/{}", trypath2);
        if(!(*funcset.libhandle_ref = dynutils::load_lib(trypath1.c_str())) &&
           !(*funcset.libhandle_ref = dynutils::load_lib(trypath2.c_str()))) {
            ld_ctx().error_string.append(fmt::format("Failed to load {:s}-{:d} (error: {:s})\n", funcset.bare_libname,
                                                     funcset.libversion, dynutils::get_error()));
            return false;
        }

#define LOAD_LIB_FUNCS_BODY(fname)                                                                    \
    failed_count += !((fname) = dynutils::load_func<fname##_t>(current_library_outer_macro, #fname)); \
    if(!(fname)) ld_ctx().error_string.append(missing_prefix_outer_macro + #fname "\n");

        // then load the functions using the x-macro list for the current lib
        // (this is for passing into the x-macro expansion)
        const std::string missing_prefix_outer_macro = fmt::format("Missing {} function: ", funcset.bare_libname);
        dynutils::lib_obj *current_library_outer_macro = *funcset.libhandle_ref;
        int failed_count = 0;

        if(funcset.bare_libname == "avutil") {
            AVUTIL_FUNCTIONS(LOAD_LIB_FUNCS_BODY);
        } else if(funcset.bare_libname == "swscale") {
            SWSCALE_FUNCTIONS(LOAD_LIB_FUNCS_BODY);
        } else if(funcset.bare_libname == "avcodec") {
            AVCODEC_FUNCTIONS(LOAD_LIB_FUNCS_BODY);
        } else if(funcset.bare_libname == "avformat") {
            AVFORMAT_FUNCTIONS(LOAD_LIB_FUNCS_BODY);
        }

#undef LOAD_LIB_FUNCS_BODY

        if(failed_count > 0) {
            ld_ctx().error_string.append(
                fmt::format("Failed to load {:d} {:s} functions\n", failed_count, funcset.bare_libname));
            return false;
        }
        // else continue to loading the next library
    }

    return true;
}

bool init_internal() {
#define FUNCSETDEF(libname, ver) \
    FFmpegFuncset { .bare_libname = #libname, .libhandle_ref = &ld_ctx().libname##_handle, .libversion = (ver) }
    // Versions of FFmpeg libraries which expose an API that's compatible with what we actually need (tested working)
    // clang-format off
    const std::array<std::array<FFmpegFuncset, 4>, 3> supported_ffmpeg_version_sets{
        {{ // FFmpeg 9.0
        FUNCSETDEF(avutil, 61),   //
        FUNCSETDEF(swscale, 10),  //
        FUNCSETDEF(avcodec, 63),  //
        FUNCSETDEF(avformat, 63), //
        }, { // FFmpeg 8.0
        FUNCSETDEF(avutil, 60),   //
        FUNCSETDEF(swscale, 9),   //
        FUNCSETDEF(avcodec, 62),  //
        FUNCSETDEF(avformat, 62), //
        }, { // FFmpeg 7.1
        FUNCSETDEF(avutil, 59),   //
        FUNCSETDEF(swscale, 8),   //
        FUNCSETDEF(avcodec, 61),  //
        FUNCSETDEF(avformat, 61), //
        }}};
    // clang-format on
#undef FUNCSETDEF

    bool succeeded = false;
    for(const auto &set : supported_ffmpeg_version_sets) {
        if(!load_full_ff_lib(set)) {
            // try the next set of versions
            ld_ctx().cleanup_internal();
        } else {
            succeeded = true;
            break;
        }
    }

    if(!succeeded) return false;

    // we would have bailed at this point if any functions/lib failed to load

    // sanity check
    void *test_ptr = av_malloc(64);
    if(test_ptr) {
        av_free(test_ptr);
    } else {
        ld_ctx().error_string.append("Dysfunctional av_malloc\n");
        ld_ctx().cleanup_internal();
        return false;
    }

    // set up log level + callback
    av_log_set_level(parse_log_level_from_string(cv::debug_ffmpeg.getString()));
    av_log_set_callback(ff_log_callback);

    ld_ctx().error_string = "";
    return true;
}

Sync::once_flag ffmpeg_init_once;
bool ffmpeg_available{false};

}  // namespace

std::string_view getInitError() { return ld_ctx().error_string; }

bool init() {
    Sync::call_once(ffmpeg_init_once, []() {
        ffmpeg_available = init_internal();
        if(!ffmpeg_available) {
            logRaw("Failed to load FFmpeg: {}", getInitError());
        }
    });
    return ffmpeg_available;
}

bool isDebugEnabled() {
    if(cv::debug_ffmpeg.isDefault()) return false;
    const auto &str = cv::debug_ffmpeg.getString();
    return (!str.empty() && str != "0" && str != "none" && str != "false");
}

}  // namespace Mc::FFmpeg

#endif  // defined(MCENGINE_FEATURE_FFMPEG)
