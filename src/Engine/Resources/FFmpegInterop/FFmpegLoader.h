// Copyright (c) 2025-2026, WH, All rights reserved.
#pragma once
#ifndef FFMPEGINTEROP_FFMPEGLOADER_H
#define FFMPEGINTEROP_FFMPEGLOADER_H
// much of this is copied from what i wrote for neoloud

#include "config.h"

#if defined(MCENGINE_FEATURE_FFMPEG)

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace Mc::FFmpeg {
namespace funcs {
// import types we need
using ::AVCodec;
using ::AVCodecContext;
using ::AVCodecParameters;
using ::AVFormatContext;
using ::AVFrame;
using ::AVIOContext;
using ::AVPacket;
using ::AVPixelFormat;
using ::AVStream;
using ::SwsContext;

// import constants that we need
using ::AV_PIX_FMT_RGBA;
using ::AVMEDIA_TYPE_VIDEO;

// define core function groups by library
#define AVFORMAT_FUNCTIONS(X)    \
    X(av_read_frame)             \
    X(avformat_alloc_context)    \
    X(avformat_close_input)      \
    X(avformat_find_stream_info) \
    X(avformat_open_input)       \
    X(avio_alloc_context)        \
    X(avio_context_free)

#define AVCODEC_FUNCTIONS(X)         \
    X(av_packet_alloc)               \
    X(av_packet_free)                \
    X(av_packet_unref)               \
    X(avcodec_alloc_context3)        \
    X(avcodec_find_decoder)          \
    X(avcodec_free_context)          \
    X(avcodec_open2)                 \
    X(avcodec_parameters_to_context) \
    X(avcodec_receive_frame)         \
    X(avcodec_send_packet)

#define AVUTIL_FUNCTIONS(X) \
    X(av_frame_alloc)       \
    X(av_frame_free)        \
    X(av_frame_unref)       \
    X(av_free)              \
    X(av_log_format_line2)  \
    X(av_log_get_level)     \
    X(av_log_set_callback)  \
    X(av_log_set_level)     \
    X(av_malloc)            \
    X(av_strerror)

#define SWSCALE_FUNCTIONS(X) \
    X(sws_alloc_context)     \
    X(sws_freeContext)       \
    X(sws_scale_frame)

#define ALL_FFMPEG_FUNCTIONS(X) \
    AVFORMAT_FUNCTIONS(X)       \
    AVCODEC_FUNCTIONS(X)        \
    AVUTIL_FUNCTIONS(X)         \
    SWSCALE_FUNCTIONS(X)

// generate function pointer types and declarations
#define DECLARE_FFMPEG_FUNCTION(name)  \
    using name##_t = decltype(::name); \
    extern name##_t *name;  // NOLINT(bugprone-macro-parentheses)

ALL_FFMPEG_FUNCTIONS(DECLARE_FFMPEG_FUNCTION)

#undef DECLARE_FFMPEG_FUNCTION

#ifdef av_err2str
#undef av_err2str
#endif

static inline char *av_make_error_string(char *errbuf, size_t errbuf_size, int errnum) {
    av_strerror(errnum, errbuf, errbuf_size);
    return errbuf;
}

#define av_err2str(errnum)                                                                         \
    ::Mc::FFmpeg::funcs::av_make_error_string(std::array<char, AV_ERROR_MAX_STRING_SIZE>{}.data(), \
                                              AV_ERROR_MAX_STRING_SIZE, errnum)

}  // namespace funcs

// open the libraries and populate the function pointers
// (false == ffmpeg unusable)
bool init();

// if init failed, this might have something
std::string_view getInitError();

// check if the debug_ffmpeg cvar is set
bool isDebugEnabled();
}  // namespace Mc::FFmpeg

#endif  // defined(MCENGINE_FEATURE_FFMPEG)
#endif  // !defined(FFMPEGINTEROP_FFMPEGLOADER_H)
