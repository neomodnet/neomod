AC_PREREQ([2.69])
AC_INIT([neosu], [[43.00]], [], [], [https://github.com/kiwec/neosu])
AC_CONFIG_SRCDIR([src/Engine/Engine.cpp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([src/config.h])
AC_PREFIX_DEFAULT([dist]) # default `make install`s to ./dist/bin/ (modify with --prefix=)

CI="${CI:-"${GITHUB_ACTIONS}"}"
# AS_IF([test "$CI_DEVBUILD" = "1"], [
#     AC_DEFINE([CI_DEVBUILD], [1], [Defined if this is an in-between-releases build.])
# ])

# add include guards to config.h, they aren't there by default for some reason
AH_TOP([
#ifndef __TOP_SRC_CONFIG_H_INCLUDE_GUARD
#define __TOP_SRC_CONFIG_H_INCLUDE_GUARD
])

AH_BOTTOM([
#endif
])

AC_DEFINE_UNQUOTED([NEOSU_VERSION], [PACKAGE_VERSION], [Defined to the version of neosu being built.])

_tstamp=$(TZ=UTC date +%y%m%d%H%M)
AC_SUBST([BUILD_TIMESTAMP], [$_tstamp])

_changelog_tstamp=$(TZ=UTC date +%Y-%m-%d)
AC_SUBST([CHANGELOG_TIMESTAMP], [$_changelog_tstamp])

# shut up wine (i need to find what part of configure is doing the "checking if we are cross compiling" check and remove it)
export DISPLAY= WAYLAND_DISPLAY= WINEDLLOVERRIDES="mscoree=;mshtml=;winex11.drv=;winewayland.drv=" WINEDEBUG=-all

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

# early emscripten detection - must happen before checks that use host_os
# emconfigure sets CC=emcc before running configure
AS_IF([[case "$CC" in
  *emcc*) WASM_CC=$CC ;;
esac]])
AS_IF([test -n "$WASM_CC"], [
    # override host triplet so subsequent checks see WASM, not linux
    host_cpu=wasm32
    host_vendor=local
    host_os=unknown-emscripten
    host=${host_cpu}-${host_vendor}-${host_os}
    host_alias=$host
    target_cpu=$host_cpu
    target_vendor=$host_vendor
    target_os=$host_os
    target=$host
    target_alias=''
    # WASM uses OpenGL ES 3.2
    AS_IF([test -z "$with_renderer"], [with_renderer=opengles])
])

AC_USE_SYSTEM_EXTENSIONS

AC_DEFUN([AX_COUNT_CPUS], [
    AC_CHECK_PROG([GETCONF], [getconf], [getconf])
    AS_IF([test -n "$GETCONF"],
        [n_cpus=$($GETCONF _NPROCESSORS_ONLN 2>/dev/null)],
        [n_cpus=4])
    AS_IF([test -z "$n_cpus" -o "$n_cpus" -le "0"], [n_cpus=4])
    AC_SUBST([CPU_COUNT], [$n_cpus])
])
AX_COUNT_CPUS

export MAKEFLAGS="-j$CPU_COUNT"
AC_SUBST([MAKEFLAGS])

# no-define needed to avoid setting e.g. VERSION which conflicts with McEngine stuff
# dist-xz for `make dist` tarball target
AM_INIT_AUTOMAKE([foreign subdir-objects no-define dist-xz parallel-tests])
AM_SILENT_RULES([yes])

AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_GREP
AC_PROG_SED

AC_DEFUN([AC_PROG_WGET], [
    AC_CHECK_PROG([WGET], [wget], [wget])
    if test -z "$WGET"; then
        AC_MSG_ERROR([wget not found - required for downloading dependencies])
    fi
])
AC_DEFUN([AC_PROG_CURLBIN], [
    AC_CHECK_PROG([CURLBIN], [curl], [curl])
    if test -z "$CURLBIN"; then
        AC_MSG_ERROR([curl not found - required for downloading dependencies])
    fi
])
AC_DEFUN([AC_PROG_GIT], [
    AC_CHECK_PROG([GIT], [git], [git])
    if test -z "$GIT"; then
        AC_MSG_ERROR([git not found - required for downloading dependencies])
    fi
])
AC_DEFUN([AC_PROG_TAR], [
    AC_CHECK_PROG([TAR], [tar], [tar])
    if test -z "$TAR"; then
        AC_MSG_ERROR([tar not found - required for extracting dependencies])
    fi
])
AC_DEFUN([AC_PROG_PKGCONF], [
    AC_CHECK_PROG([PKGCONF], [pkgconf], [pkgconf])
    if test -z "$PKGCONF"; then
        AC_MSG_ERROR([pkgconf not found - required for finding include/library paths])
    fi
])
AC_DEFUN([AC_PROG_CMAKE], [
    AC_CHECK_PROG([CMAKE], [cmake], [cmake])
    if test -z "$CMAKE"; then
        AC_MSG_ERROR([cmake not found - required for building dependencies])
    fi
])
AC_DEFUN([AC_PROG_PYTHON], [
    AC_CHECK_PROG([PYTHON], [python3], [python3])
    if test -z "$PYTHON"; then
        AC_CHECK_PROG([PYTHON], [python], [python])
    fi
])
AC_DEFUN([AC_PROG_MESON], [
    AC_CHECK_PROG([MESON], [meson], [meson])
    if test -z "$MESON"; then
        AC_MSG_ERROR([meson not found - required for building dependencies])
    fi
])
AC_DEFUN([AC_PROG_NINJA], [
    AC_CHECK_PROG([NINJA], [ninja], [ninja])
    if test -z "$NINJA"; then
        AC_MSG_ERROR([ninja not found - required for building dependencies])
    fi
])
AC_DEFUN([AC_PROG_UNZIP], [
    AC_CHECK_PROG([UNZIP], [unzip], [unzip])
    if test -z "$UNZIP"; then
        AC_MSG_ERROR([unzip not found - required for extracting dependencies])
    fi
])
AC_DEFUN([AC_PROG_PATCHELF], [
    AC_CHECK_PROG([PATCHELF], [patchelf], [patchelf])
    if test -z "$PATCHELF"; then
        AC_MSG_ERROR([patchelf not found - required for fixing up dependencies])
    fi
])
# needed for building libjpeg (if building it is required)
AC_DEFUN([AC_PROG_NASM], [
    AC_CHECK_PROG([NASM], [nasm], [nasm])
    if test -z "$NASM"; then
        AC_MSG_ERROR([nasm not found - required for building libjpeg])
    fi
])

# needed for building SPIR-V shaders (if building it is required)
# tries glslc first (from shaderc), falls back to glslangValidator (from glslang-tools)
AC_DEFUN([AC_PROG_GLSLC], [
    AC_CHECK_PROG([GLSLC], [glslc], [glslc])
    if test -z "$GLSLC"; then
        AC_CHECK_PROG([GLSLANG_VALIDATOR], [glslangValidator], [glslangValidator])
        if test -z "$GLSLANG_VALIDATOR"; then
            AC_MSG_ERROR([neither glslc nor glslangValidator found - one is required for compiling GLSL shaders to SPIR-V])
        fi
        GLSLC="$GLSLANG_VALIDATOR"
    fi
])


# needed for dep resolver
#AC_PROG_WGET
AC_PROG_CURLBIN
#AC_PROG_GIT
AC_PROG_TAR
AC_PROG_PKGCONF
AC_PROG_NINJA
AC_PROG_UNZIP
AC_PROG_MESON
AC_PROG_CMAKE
AC_PROG_PYTHON
AC_SUBST([PYTHON])

AC_CHECK_PROG([BEAR], [bear], [bear])
AC_SUBST([BEAR])

AC_CHECK_PROG([WINE], [wine], [wine]) # this feels stupid, but i wanna make a shortcut from outside a windows environment to add "-sound xyz" for non-default audio subsystems
AC_SUBST([WINE])

AC_CHECK_PROG([MOLD], [mold], [mold])
AC_CHECK_PROG([LLD], [lld], [lld])

AC_ARG_ENABLE([optimize],
    AS_HELP_STRING([--enable-optimize], [Optimize for speed (default: yes)]))
AC_ARG_ENABLE([native],
    AS_HELP_STRING([--enable-native], [Compile for -march=native (default: no)])) # currently broken for windows
AC_ARG_ENABLE([debug],
    AS_HELP_STRING([--enable-debug], [Enable debug build (default: no)]))
AC_ARG_ENABLE([harden],
    AS_HELP_STRING([--enable-harden], [Make a hardened build (default: yes)]))
AC_ARG_ENABLE([strip],
    AS_HELP_STRING([--enable-strip], [Strip the final binary (default: no for debug, yes otherwise)]))
AC_ARG_ENABLE([asan],
    AS_HELP_STRING([--enable-asan], [Enable ASan/UBSan (turns debug on as well) (default: no)]))
AC_ARG_ENABLE([tsan],
    AS_HELP_STRING([--enable-tsan], [Enable TSan/UBSan (turns debug on as well) (default: no)]))
AC_ARG_ENABLE([clang],
    AS_HELP_STRING([--enable-clang], [Use clang/clang++ instead of gcc/g++ (default: no)]))
AC_ARG_ENABLE([mold],
    AS_HELP_STRING([--enable-mold], [Use the mold linker instead of lld/bfd (faster link time, non-Windows only) (default: off if LTO is enabled, on (if available) otherwise)]))
AC_ARG_ENABLE([omp],
    AS_HELP_STRING([--enable-omp], [Use and link with OpenMP for faster parallel processing (default: no)]))
AC_ARG_ENABLE([ime],
    AS_HELP_STRING([--enable-ime], [Enable (SDL) IME support on Windows/Linux Wayland (default: yes)]))
AC_ARG_ENABLE([lto],
    AS_HELP_STRING([--enable-lto], [Enable Link Time Optimization (default: auto, use if functional)]))
AC_ARG_ENABLE([system-deps],
    AS_HELP_STRING([--enable-system-deps], [Prefer system libraries over locally built ones (not recommended! If you do this, also recommend using --disable-static) (default: no)]))
AC_ARG_ENABLE([static],
    AS_HELP_STRING([--enable-static], [Enable static linking where possible (default: yes, --disable-static implies shared)]))
AC_ARG_ENABLE([werror],
    AS_HELP_STRING([--enable-werror], [Build with -Werror (default: yes)]))
AC_ARG_ENABLE([pch],
    AS_HELP_STRING([--enable-pch], [Build and use a pre-compiled header to speed up rebuild time (default: yes)]))
AC_ARG_ENABLE([redistributable],
    AS_HELP_STRING([--enable-redistributable], [Don't bundle any non-redistributable content (currently only relevant for WASM, skips bundling beatmaps, default: yes)]))

AC_ARG_WITH([renderer],
    AS_HELP_STRING([--with-renderer={opengl,dx11,opengles,sdlgpu}], [The (comma separated list of) renderer backend(s) to build with (default: opengl)]))

AC_ARG_WITH([audio],
    AS_HELP_STRING([--with-audio={bass,soloud}], [The (comma separated list of) audio backend(s) to build with (default: soloud)]))

AC_ARG_WITH([mimalloc],
    AS_HELP_STRING([--with-mimalloc], [Use mimalloc instead of the default memory allocator (faster) (default: on if not debug and not winx86)]))

AC_ARG_WITH([nsync],
    AS_HELP_STRING([--with-nsync], [Use nsync instead of std::mutex for synchronization (faster) (default: on if not winx86 (unsupported))]))

AC_ARG_WITH([discord],
    AS_HELP_STRING([--with-discord], [Build with Discord rich presence support (default on if supported (Windows, Linux 64-bit))]))

# setup defaults
AS_IF([test "x$enable_redistributable" != "xno"], [
    enable_redistributable=yes
    disable_redistributable=no
], [
    enable_redistributable=no
    disable_redistributable=yes
])

AS_IF([test "x$enable_static" != "xno"], [
    enable_static=yes
    disable_static=no
    enable_shared=no
    disable_shared=yes

    pc_addflags="--static"
    STATICBUILD=yes
], [
    enable_static=no
    disable_static=yes
    enable_shared=yes
    disable_shared=no
])

# compatibility, disable unless explicitly requested
AS_IF([test "x$enable_native" != "xyes"], [
    enable_native=no
    disable_native=yes

    DISABLE_NATIVE=yes
], [
    enable_native=yes
    disable_native=no
])

AC_SUBST([DISABLE_NATIVE])
AC_SUBST([STATICBUILD])

AS_IF([test "x$enable_system_deps" != "xyes"], [
    enable_system_deps=no
    disable_system_deps=yes
], [
    enable_system_deps=yes
    disable_system_deps=no
])

AS_IF([test "x$enable_ime" != "xno"], [
    enable_ime=yes
    disable_ime=no
    AC_DEFINE([MCENGINE_FEATURE_IMESUPPORT], [1], [Defined if an IME may be used.])
], [
    enable_ime=no
    disable_ime=yes

    NOIME="yes"
])

AS_IF([test "x$with_discord" != "xno" && { test "$host_os" = "mingw32" || { test "$host_os" = "linux-gnu" && test "$host_cpu" = "x86_64" ; } ; } ], [
    with_discord=yes
    AC_DEFINE([MCENGINE_FEATURE_DISCORD], [1], [Defined if building with Discord RPC integration.])
], [
    with_discord=no
])

AC_SUBST([NOIME])

# set some default flags here
CFLAGS="$CFLAGS -fvisibility=hidden"
CXXFLAGS="$CXXFLAGS -fvisibility=hidden"
LDFLAGS="$LDFLAGS -fvisibility=hidden"

AS_IF([test "$host_os" = "linux-gnu"], [
    AC_PROG_PATCHELF # only need this for linux
])

AS_IF([test "$host_os" = "mingw32"], [
    NEOSU_CPPFLAGS="$NEOSU_CPPFLAGS -D_UNICODE -DUNICODE=1"
    NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -D_UNICODE -DUNICODE=1 -municode"
    NEOSU_LDFLAGS="$NEOSU_LDFLAGS -municode"
    AS_IF([test "x$enable_clang" = "xyes"], [
        LLVMMINGW=1
        AC_SUBST([LLVMMINGW])
        CXXFLAGS="$CXXFLAGS --target=$host_cpu-w64-windows-gnu"
        CFLAGS="$CFLAGS --target=$host_cpu-w64-windows-gnu"
    ])
])

use_opengl_default=yes # ugly
use_opengl=unset
use_opengles32=unset
use_dx11=unset
use_sdlgpu=unset

AS_VAR_SET_IF([with_renderer],
    [ac_save_IFS=$IFS
        IFS=' ,'
        set x $with_renderer
        IFS=$ac_save_IFS
        shift
        for rend
        do
            case $rend in
            opengl) use_opengl=yes ;;
            opengles*) use_opengles32=yes ;;
            dx11) use_dx11=yes ;;
            directx*) use_dx11=yes ;;
            *sdl*) use_sdlgpu=yes ;;
            *gpu*) use_sdlgpu=yes ;;
            esac
        done])

AS_IF([test "x$use_dx11" = "xyes" || test "x$use_opengles32" = "xyes" || test "x$use_sdlgpu" = "xyes"], [
    use_opengl_default=no
    AS_IF([test "x$use_opengles32" = "xyes"], [
        # can't combine GL and GL ES
        use_opengl=no
    ])
])

rendbackendlist=

AS_IF([test "x$use_opengl" = "xyes" || test "x$use_opengl_default" = "xyes"], [
    use_opengl=yes
    AC_DEFINE([MCENGINE_FEATURE_OPENGL], [1], [Defined if the OpenGL backend is to be used.])
    rendbackendlist="opengl"
], [
    use_opengl=no
])

AS_IF([test "x$use_opengles32" = "xyes" && test "x$use_opengl" != "yes"], [
    # TODO
    use_opengles32=yes
    AC_DEFINE([MCENGINE_FEATURE_GLES32], [1], [Defined if the OpenGL ES backend is to be used.])
    rendbackendlist="opengles32"
], [
    use_opengles32=no
])

AS_IF([test "x$use_dx11" = "xyes" ], [
    use_dx11=yes
    AC_DEFINE([MCENGINE_FEATURE_DIRECTX11], [1], [Defined if the DX11 backend is to be used.])
    rendbackendlist="$rendbackendlist dx11"
], [
    use_dx11=no
])

AS_IF([test "x$use_sdlgpu" = "xyes" ], [
    AC_PROG_GLSLC
    AS_IF([test "$host_os" = "mingw32"], [
        # needed for compiling HLSL shaders to DXIL (Windows/D3D12 only)
        # will download & cache a release from github if not available (only if building on linux though (lazy))
        AC_CHECK_PROG([DXC], [dxc], [dxc])
        AS_IF([test -z "$DXC" && test "$build_os" != "linux-gnu"], [
            AC_MSG_ERROR([dxc not found - required for compiling HLSL shaders to DXIL])
        ])
    ])

    use_sdlgpu=yes
    AC_DEFINE([MCENGINE_FEATURE_SDLGPU], [1], [Defined if the SDL_gpu backend is to be used.])
    rendbackendlist="$rendbackendlist sdlgpu"
], [
    use_sdlgpu=no
])

# build the list of binary embed manifests based on renderer selection
EMBED_MANIFESTS="\$(srcdir)/\$(ASSETS_DIR)/binary/misc_embed.manifest"
AS_IF([test "x$use_opengl" = "xyes" || test "x$use_opengles32" = "xyes"], [
    EMBED_MANIFESTS="$EMBED_MANIFESTS \$(srcdir)/\$(ASSETS_DIR)/binary/gl_shaders_embed.manifest"
])
AS_IF([test "x$use_dx11" = "xyes"], [
    EMBED_MANIFESTS="$EMBED_MANIFESTS \$(srcdir)/\$(ASSETS_DIR)/binary/dx_shaders_embed.manifest"
])

# SDL_gpu shader packs are compiled and embedded via pack_sdlgpu_shader.py,
# not through a static manifest (see Makefile.am).
AC_SUBST([EMBED_MANIFESTS])

AUDBACKENDLIST=""

with_bass=unset
with_soloud=unset
AS_VAR_SET_IF([with_audio],
    [ac_save_IFS=$IFS
        IFS=' ,'
        set x $with_audio
        IFS=$ac_save_IFS
        shift
        for aud
        do
            case $aud in
            soloud*) with_soloud=yes ;;
            bass*) with_bass=yes ;;
            esac
        done])

AS_IF([test "$with_bass" = "yes" && ! { test "$host_cpu" = "x86_64" || test "$host_cpu" = "i686" ; }], [
    AC_MSG_NOTICE([BASS is currently unsupported on $host_cpu. Using SoLoud.])
    with_bass=no
])

AS_IF([test "$with_soloud" = "yes" || { test "$with_soloud" = "unset" && test "$with_bass" != "yes"; } ], [
    AUDBACKENDLIST="${AUDBACKENDLIST} SoLoud"
    with_soloud=yes
    AC_DEFINE([MCENGINE_FEATURE_SOLOUD], [1], [Define if the SoLoud audio backend is desired.])
], [
    with_soloud=no
])

AS_IF([test "$with_bass" = "yes"], [
    AS_IF([test "$with_soloud" = "yes"], [
        make_audio_shortcuts=yes
    ])
    AUDBACKENDLIST="${AUDBACKENDLIST} BASS"
    AC_DEFINE([MCENGINE_FEATURE_BASS], [1], [Define if the BASS audio backend is desired.])
], [
    with_bass=no
])

AC_SUBST([AUDBACKENDLIST])

AS_IF([test "x$enable_asan" = "xyes" || test "x$enable_tsan" = "xyes"], [
    sanitizer_string=
    AS_IF([test "x$enable_asan" = "xyes"], [
        sanitizer_string=" -fsanitize=address,undefined"
    ], [
        sanitizer_string=" -fsanitize=undefined,thread"
    ])

    CXXFLAGS="${CXXFLAGS}${sanitizer_string}"
    CFLAGS="${CFLAGS}${sanitizer_string}"
    LDFLAGS="${LDFLAGS}${sanitizer_string}" # apparently it also needs to be specified as a linker flag...?

    AS_IF([test -z "$LLVMMINGW" && test "x$enable_clang" = "xyes"],[
        CFLAGS="$CFLAGS -fsanitize-address-use-after-scope -fsanitize-address-use-after-return=always"
        CXXFLAGS="$CXXFLAGS -fsanitize-address-use-after-scope -fsanitize-address-use-after-return=always"
    ])
    enable_debug="yes"
    ENABLE_ASAN=$enable_asan
    AC_SUBST([ENABLE_ASAN])
])

# disable lto for debug by default (build time opt)
AS_IF([test "x$enable_debug" = "xyes" && test "x$enable_lto" != "xyes"], [
    enable_lto=no
    disable_lto=yes
])

# disable -Werror for debug by default
AS_IF([test "x$enable_debug" = "xyes" && test "x$enable_werror" != "xyes"], [
    enable_werror=no
    disable_werror=yes
])

# enable lto by default otherwise
AS_IF([test "x$enable_lto" != "xno"], [
    disable_lto=no
])

# detect emscripten (CC=emcc when using emconfigure)
AS_IF([[case "$CC" in
  *emcc*) WASM_CC=$CC ;;
esac]])

AS_IF([test -n "$WASM_CC"], [
    # emscripten/WASM build
    CXXFLAGS="$CXXFLAGS -sWASM_WORKERS=1 -pthread -msimd128 -Qunused-arguments -Wno-unused-command-line-argument -Wno-c2x-extensions"
    CFLAGS="$CFLAGS -sWASM_WORKERS=1 -pthread -msimd128 -Qunused-arguments -Wno-unused-command-line-argument"
    LDFLAGS="$LDFLAGS -sWASM_WORKERS=1 -pthread -msimd128 -Qunused-arguments -Wno-unused-command-line-argument"
    _ccache_ctype="clang"
    _cid="Emscripten"
], [
    AS_IF([test "x$enable_clang" = "xyes"], [
        CC=clang
        CXX=clang++
        CXXFLAGS="$CXXFLAGS -Qunused-arguments -Wno-c2x-extensions"
        CFLAGS="$CFLAGS -Qunused-arguments"
        LDFLAGS="$LDFLAGS"
        CLANG=yes
        _ccache_ctype="clang"
        _cid="Clang"
        AC_SUBST([CLANG])
    ],[
        CC=gcc
        CXX=g++
        CXXFLAGS="$CXXFLAGS -fno-semantic-interposition"
        CFLAGS="$CFLAGS -fno-semantic-interposition"
        NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -fimplicit-constexpr"
        _ccache_ctype="gcc"
        _cid="GNU"
    ])
])

# WASM-specific defaults: disable nsync+mimalloc unless explicitly requested
AS_IF([test -n "$WASM_CC"], [
    # python is required for WASM builds (for generating #embed source files)
    AS_IF([test -z "$PYTHON"], [
        AC_MSG_ERROR([python3 not found - required for WASM builds to generate embedded binary sources])
    ])
    # nsync doesn't build properly with emscripten
    AS_IF([test "x$with_nsync" != "xyes"], [
        with_nsync=no
    ])
    # mimalloc has issues with emscripten's -xnone handling
    AS_IF([test "x$with_mimalloc" != "xyes"], [
        with_mimalloc=no
    ])
])

AS_IF([test "$host_cpu" = "x86_64" && test -z "$LLVMMINGW" && test -z "$WASM_CC"], [
    # this is only possible on x86_64, and not with llvm-mingw or wasm
    # faster tls access
    CFLAGS="$CFLAGS -mtls-dialect=gnu2"
    CXXFLAGS="$CXXFLAGS -mtls-dialect=gnu2"
])

AS_IF([[case "$host_cpu" in
  i[[3456789]]86*) host_cpu=i686 ;;
esac]])

ac_cv_env_PKG_CONFIG_value="$PKG_CONFIG"
ac_cv_env_PKG_CONFIG_set="set"

PKG_CONFIG_PATH="$PWD/build/deps/lib/pkgconfig:$PWD/build/deps/lib64/pkgconfig"
PKG_CONFIG_PATH_CUSTOM="$PKG_CONFIG_PATH"
AC_SUBST([PKG_CONFIG_PATH])
AC_SUBST([PKG_CONFIG_PATH_CUSTOM])

win_subsystem_ver=6.00
AS_IF([test -n "$WASM_CC"], [
    # WASM/Emscripten build
    WASM_POLYFILL_DEP="\$(srcdir)/src/Platform/WASM/wasm-node-polyfill.js"
    emscripten_flags="-sPTHREAD_POOL_SIZE=32 -sAUDIO_WORKLET=1 -sWASM=1 -sWASM_WORKERS=1 -sUSE_PTHREADS=1 -sFETCH=1"
    emscripten_flags="$emscripten_flags -sMAX_WEBGL_VERSION=2 -sLLD_REPORT_UNDEFINED -sSTACK_SIZE=1MB -sENVIRONMENT=web,worker,node"
    emscripten_flags="$emscripten_flags -sWEBSOCKET_URL=wss:// -sFULL_ES3 -sINITIAL_MEMORY=1536MB"
    emscripten_flags="$emscripten_flags -sEXPORTED_RUNTIME_METHODS=callMain,ENV -sINCOMING_MODULE_JS_API=locateFile,onAbort,onRuntimeInitialized,noInitialRun,print"

    NEOSU_CPPFLAGS="$NEOSU_CPPFLAGS -U__linux__ -D__EMSCRIPTEN__"
    NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS $emscripten_flags"
    # --emrun for support for running the .html with emrun (useful for offline testing)
    NEOSU_LDFLAGS="$NEOSU_LDFLAGS $emscripten_flags -lidbfs.js -lnodefs.js --pre-js $WASM_POLYFILL_DEP --emrun"

    WASM_PRELOAD_FLAGS="--preload-file \$(srcdir)/\$(ASSETS_DIR)/fonts@fonts --preload-file \$(srcdir)/\$(ASSETS_DIR)/materials@materials"
    AS_IF([test "x$enable_redistributable" = "xno"], [
        # bundle some copyrighted beatmaps for non-redistributable builds
        # useful for testing purposes to have something to immediately play
        WASM_PRELOAD_FLAGS="$WASM_PRELOAD_FLAGS --preload-file \$(builddir)/wasm-maps@/libsdl/Songs"
    ])
    AC_SUBST([WASM_PRELOAD_FLAGS])
    AC_SUBST([WASM_POLYFILL_DEP])

    # pkg-config wrapper for emscripten
    install -Dm755 /dev/stdin $PWD/build/pkg-config <<__EOF__
#!/usr/bin/env bash
PKG_CONFIG_PATH="$PKG_CONFIG_PATH" $PKGCONF $pc_addflags "\$@"
__EOF__

    # forward these for building dependencies
    AC_SUBST([CMAKE_CONF_PFX], [emcmake])
    AC_SUBST([MAKE_PFX], [emmake])
    AC_SUBST([CONFIGURE_PFX], [emconfigure])

    CMAKE_SYSTEM_NAME="Emscripten"
    CMAKE_GEN="Ninja"
    SYSNAME="wasm"
    DLLEXT=".so"
    LIBPREFIX="lib"
    EXEEXT=".html"
    AC_DEFINE([MCENGINE_PLATFORM_WASM], [1], [Define if compiling for WASM with Emscripten.])
    AC_DEFINE([NEOSU_DATA_DIR], ["/persist/"], [Persistent writable data directory for WASM.])
], [test "$host_os" = "linux-gnu"], [
    AS_IF([test "$host_cpu" = "i686"], [
        install -Dm755 /dev/stdin $PWD/build/$target-$CC <<EOF
#!/usr/bin/env bash
$CC -m32 "\$@"
EOF
        install -Dm755 /dev/stdin $PWD/build/$target-$CXX <<EOF
#!/usr/bin/env bash
$CXX -m32 "\$@"
EOF
        AS_IF([test "x$enable_clang" = "xyes"], [
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -latomic"
        ])
        # ld complains about libcrypto.a(libcrypto-lib-cast-586.o): warning: relocation against `CAST_S_table1' in read-only section `.text'
        # TODO: rebuild curl/openssl with -fPIE maybe?
        NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -fno-PIE -no-pie"
        NEOSU_LDFLAGS="$NEOSU_LDFLAGS -no-pie"
        AC_SUBST([NASMOBJ],[elf32])
    ],[
        install -Dm755 /dev/stdin $PWD/build/$target-$CC <<EOF
#!/usr/bin/env bash
$CC "\$@"
EOF
        install -Dm755 /dev/stdin $PWD/build/$target-$CXX <<EOF
#!/usr/bin/env bash
$CXX "\$@"
EOF
        AC_SUBST([NASMOBJ],[elf64])
    ])

    install -Dm755 /dev/stdin $PWD/build/pkg-config <<__EOF__
#!/usr/bin/env bash
PKG_CONFIG_PATH="$PKG_CONFIG_PATH" $PKGCONF --personality=$host_cpu-pc-linux-gnu $pc_addflags "\$@"
__EOF__

    CC="$PWD/build/$target-$CC"
    CXX="$PWD/build/$target-$CXX"
    ac_tool_prefix=
    CMAKE_SYSTEM_NAME="Linux"
    CMAKE_GEN="Ninja"
    SYSNAME="linux"
    DLLEXT=".so"
    LIBPREFIX="lib"

    # fixup linker runtime paths
    NATIVE_LDFLAGS="-Wl,-rpath='\$\$ORIGIN/lib',-rpath='\$\$ORIGIN'"
    AC_SUBST([NATIVE_LDFLAGS])

    AC_DEFINE([MCENGINE_PLATFORM_LINUX], [1], [Define if compiling for Linux.])
],[
    AC_CHECK_TOOL([WINDRES], [windres], [windres])

    CMAKE_SYSTEM_NAME="Windows"
    CMAKERCOPT="-DCMAKE_RC_COMPILER=$WINDRES"
    SYSNAME="windows"
    DLLEXT=".dll"
    LIBPREFIX=""

    AS_IF([test "$host_cpu" = "i686"], [ # uber compatibility configuration, works on xp
        ubercompat=on
        with_mimalloc="no"
        LDFLAGS="$LDFLAGS -m32 -Wl,--large-address-aware"
        # xp
        win_subsystem_ver="5.01"
        CPPFLAGS="$CPPFLAGS -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -D_WIN32_WINDOWS=0x0501 -D_WIN32_IE=0x0501"
        CFLAGS="$CFLAGS -m32 -fno-omit-frame-pointer -momit-leaf-frame-pointer -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -D_WIN32_WINDOWS=0x0501 -D_WIN32_IE=0x0501 -mfpmath=sse"
        CXXFLAGS="$CXXFLAGS -m32 -fno-omit-frame-pointer -momit-leaf-frame-pointer -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -D_WIN32_WINDOWS=0x0501 -D_WIN32_IE=0x0501 -mfpmath=sse"
        AC_SUBST([NASMOBJ],[win32])
    ], [
        # win10
        CPPFLAGS="$CPPFLAGS -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_WINDOWS=0x0A00 -D_WIN32_IE=0x0A00"
        CFLAGS="$CFLAGS -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_WINDOWS=0x0A00 -D_WIN32_IE=0x0A00"
        CXXFLAGS="$CXXFLAGS -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_WINDOWS=0x0A00 -D_WIN32_IE=0x0A00"
        AC_SUBST([NASMOBJ],[win64])
    ])

    # increase default stack size to 8mb from 1mb
    NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,--stack,8388608"
    AC_DEFINE([MCENGINE_PLATFORM_WINDOWS], [1], [Define if compiling for Windows.])
])

AC_PROG_CC([$CC])
AS_IF([[case "$CC" in
  *clang) CXX=$CC++ ;;
  *gcc) CXX=${CC%gcc}g++ ;;
esac]])
AC_PROG_CXX([$CXX])

AC_CHECK_PROG([STRIP], [strip], [strip]) # don't use mingw-strip, seems to not work as well as regular binutils strip even for windows builds...

AC_CHECK_PROG([CCACHE], [ccache], [ccache])

# we already set $CC and $CXX to absolute paths in the build/$arch-pc-linux-gnu-{$CC,$CXX} wrappers
# for linux, so AC_PATH_PROG doesn't work for that (and isn't needed anyways)
# this is done mostly for mingw targets so that configure --host=x86_64-w64-mingw32 --enable-clang
# saves the full path to the llvm-mingw compiler, in case $PATH changes for future compiles
# for wasm, emconfigure sets CC/CXX to full paths already
AS_IF([test "$host_os" = "linux-gnu" || test -n "$WASM_CC"],[
    AS_VAR_SET([CC_REAL], [$CC])
    AS_VAR_SET([CXX_REAL], [$CXX])
],[
    AC_PATH_PROG([CC_REAL], [$CC])
    AC_PATH_PROG([CXX_REAL], [$CXX])
])

_cc_real_hash="$($CC --version | md5sum | cut -d ' ' -f 1),$host"
_cxx_real_hash="$($CXX --version | md5sum | cut -d ' ' -f 1),$host"

install -Dm755 /dev/stdin $PWD/build/cc <<EOF
#!/usr/bin/env bash
CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \\
CCACHE_COMPRESS=1 CCACHE_COMPRESSLEVEL=4 CCACHE_COMPILERTYPE=$_ccache_ctype CCACHE_COMPILERCHECK="string:$_cc_real_hash" \\
$CCACHE $CC_REAL "\$@"
EOF
install -Dm755 /dev/stdin $PWD/build/cxx <<EOF
#!/usr/bin/env bash
CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \\
CCACHE_COMPRESS=1 CCACHE_COMPRESSLEVEL=4 CCACHE_COMPILERTYPE=$_ccache_ctype CCACHE_COMPILERCHECK="string:$_cxx_real_hash" \\
$CCACHE $CXX_REAL "\$@"
EOF

export CC="$PWD/build/cc"
export CXX="$PWD/build/cxx"

AC_SUBST([CC])
AC_SUBST([CXX])

# linker setup
using_mold=no
AS_IF([test -n "$MOLD" && test "$host_os" = "linux-gnu" &&
      { test "x$enable_mold" = "xyes" || { test "x$enable_mold" != "xno" && test "x$enable_lto" = "xno" ; } ; }], [
    AX_CHECK_COMPILE_FLAG([-fuse-ld=mold], [
        AX_CHECK_LINK_FLAG([-fuse-ld=mold], [
            CXXFLAGS="$CXXFLAGS -fuse-ld=mold"
            CFLAGS="$CFLAGS -fuse-ld=mold"
            LDFLAGS="$LDFLAGS -fuse-ld=mold"
            using_mold=yes
        ])
    ])
])

AS_IF([test -n "$LLD" && test "x$using_mold" != "xyes" && test "x$enable_clang" = "xyes"], [
    AX_CHECK_COMPILE_FLAG([-fuse-ld=lld], [
        AX_CHECK_LINK_FLAG([-fuse-ld=lld], [
            CXXFLAGS="$CXXFLAGS -fuse-ld=lld"
            CFLAGS="$CFLAGS -fuse-ld=lld"
            LDFLAGS="$LDFLAGS -fuse-ld=lld"
        ])
    ])
])

# putting this part here so that $CC is defined to the final value
AS_IF([test "$host_os" = "mingw32"], [
    WINPTHREAD_LDFLAGS=""
    AS_IF([test "x$enable_static" = "xyes"], [
        # locate the original winpthread library
        WINPTHREAD_ORIG=$(${CC} -print-file-name=libwinpthread.a)
        AC_SUBST([WINPTHREAD_ORIG])
        WINPTHREAD_LDFLAGS="-L\$(DEPS_PREFIX)/lib -Wl,--whole-archive,-Bstatic -l:libwinpthread_noversion.a -Wl,--no-whole-archive,-Bdynamic"
    ])
    AC_SUBST([WINPTHREAD_LDFLAGS])

    # building on linux
    AS_IF([test "$build_os" = "linux-gnu" || test "$build_os" = "linux-musl"], [
        AC_SUBST([PKG_CONFIG_SYSTEM_LIBRARY_PATH])
        AC_SUBST([PKG_CONFIG_SYSTEM_INCLUDE_PATH])
        AC_SUBST([PKG_CONFIG_LIBDIR])

        # set library search paths if not already provided
        AS_VAR_SET_IF([PKG_CONFIG_SYSTEM_LIBRARY_PATH],
            [pc_syslibpath="$PKG_CONFIG_SYSTEM_LIBRARY_PATH"], [
            pc_syslibpath="$($CC -print-search-dirs | grep '^libraries:' | sed 's/libraries: =//')"
        ])

        # set include search paths if not already provided
        AS_VAR_SET_IF([PKG_CONFIG_SYSTEM_INCLUDE_PATH],
            [pc_sysincpath="$PKG_CONFIG_SYSTEM_INCLUDE_PATH"], [
            pc_sysincpath="$($CC -E -v -x c /dev/null 2>&1 >/dev/null | \
                sed -n '/#include <...> search starts here:/,/End of search list./p' | \
                sed '1d;$d' | sed 's/^ //' | tr '\n' ':' | sed 's/:$//')"
        ])

        # set pkg-config library directory if not already provided
        AS_VAR_SET_IF([PKG_CONFIG_LIBDIR],
            [pc_syslibdir="$PKG_CONFIG_LIBDIR"], [
            AS_IF([test "x$enable_system_deps" = "xyes" && test -d "/usr/$host_cpu-w64-mingw32/lib/pkgconfig"],
                [pc_syslibdir="/usr/$host_cpu-w64-mingw32/lib/pkgconfig"],
                [pc_syslibdir=""])
        ])

        install -Dm755 /dev/stdin $PWD/build/pkg-config <<__EOF__
#!/usr/bin/env bash
PKG_CONFIG_LIBDIR="$pc_syslibdir" \\
PKG_CONFIG_SYSTEM_INCLUDE_PATH="$pc_sysincpath" \\
PKG_CONFIG_SYSTEM_LIBRARY_PATH="$pc_syslibpath" \\
PKG_CONFIG_PATH="$PKG_CONFIG_PATH" \\
$PKGCONF --personality=$host_cpu-w64-mingw32 $pc_addflags "\$@"
__EOF__

        CMAKE_GEN="Ninja"
    ], [
        # WIP: building in msys2
        install -Dm755 /dev/stdin $PWD/build/pkg-config <<__EOF__
#!/usr/bin/env bash
PKG_CONFIG_PATH="$PKG_CONFIG_PATH" \
$PKGCONF --personality=$host_cpu-w64-mingw32 $pc_addflags "\$@"
__EOF__
        CMAKE_GEN="MSYS Makefiles"
        CMAKEIDOPT="-DCMAKE_CXX_COMPILER_ID=$_cid -DCMAKE_C_COMPILER_ID=$_cid"
        CFLAGS="$CFLAGS -UHAVE_SYMLINK" # for some reason libarchive's configure thinks we have it then fails to compile...
        MSYS2_BUILD=1
    ])
])

PKG_CONFIG="$PWD/build/pkg-config"
AC_SUBST([PKG_CONFIG])
AC_SUBST([CMAKE_SYSTEM_NAME])
AC_SUBST([CMAKERCOPT])
AC_SUBST([CMAKEIDOPT])
CMAKEPAROPT="--parallel $CPU_COUNT"
AC_SUBST([CMAKEPAROPT])
AC_SUBST([CMAKE_GEN])
AC_SUBST([SYSNAME])
AC_SUBST([DLLEXT])
AC_SUBST([LIBPREFIX])

# don't omit frame pointers, it's not worth it on 64-bit, and having them makes profiling/debugging better
AS_IF([test "$host_cpu" != "i686" && test -z "$WASM_CC"], [COMMON_FLAGS="$COMMON_FLAGS -mcmodel=small -fno-omit-frame-pointer -momit-leaf-frame-pointer"])

CPPFLAGS="$CPPFLAGS -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64"
AS_IF([test -z "$WASM_CC"], [
    COMMON_FLAGS="$COMMON_FLAGS -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64 -pipe -ffunction-sections -fdata-sections -fwrapv -fno-strict-aliasing -ftls-model=initial-exec -pthread"
    LDFLAGS="$LDFLAGS -static-libstdc++ -static-libgcc -pthread"
], [
    COMMON_FLAGS="$COMMON_FLAGS -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64 -pipe -ffunction-sections -fdata-sections -fwrapv -fno-strict-aliasing"
])
CXXFLAGS="$CXXFLAGS $COMMON_FLAGS -fno-exceptions" # -fno-unwind-tables -fno-rtti
CFLAGS="$CFLAGS $COMMON_FLAGS"

AC_SUBST([CXXFLAGS])
AC_SUBST([CFLAGS])
AC_SUBST([LDFLAGS])
AC_SUBST([CXXFLAGS_EXTRA]) # appended to neosu build flags only

AC_LANG_PUSH([C++])
AS_IF([test "x$enable_native" != "xno"], [
    CXXFLAGS="$CXXFLAGS -march=native -mtune=native"
    CFLAGS="$CFLAGS -march=native -mtune=native"
],[
    AS_IF([test "$host_cpu" = "i686"], [
        CXXFLAGS="$CXXFLAGS -march=pentium4 -mtune=generic"
        CFLAGS="$CFLAGS -march=pentium4 -mtune=generic"
    ], [
        AS_IF([test "$host_cpu" = "x86_64"], [
            CXXFLAGS="$CXXFLAGS -march=nocona -mtune=core-avx2"
            CFLAGS="$CFLAGS -march=nocona -mtune=core-avx2"
        ], [
            AS_IF([test "$host_cpu" = "aarch64"], [
                CXXFLAGS="$CXXFLAGS -march=armv8.2-a+fp16+simd -mtune=cortex-a76"
                CFLAGS="$CFLAGS -march=armv8.2-a+fp16+simd -mtune=cortex-a76"
            ])
        ])
    ])
])
# use c/c++23 because we can
AX_CHECK_COMPILE_FLAG([-std=gnu++23], [
    CXXFLAGS="$CXXFLAGS -std=gnu++23"
], [
    AC_MSG_ERROR([Compiler does not support gnu++23])
])

AC_PATH_TOOL([LD], [ld], [AC_MSG_ERROR([ld not found])])
AS_IF([test "x$enable_clang" = "xyes" && test "$host_os" = "linux-gnu"], [
    AC_PATH_PROG([AR], [llvm-ar], [llvm-ar], [AC_MSG_ERROR([llvm-ar not found])])
    AC_PATH_PROG([RANLIB], [llvm-ranlib], [llvm-ranlib], [AC_MSG_ERROR([llvm-ranlib not found])])
], [
    AC_PATH_TOOL([AR], [ar], [AC_MSG_ERROR([ar not found])])
    AC_PATH_TOOL([RANLIB], [ranlib], [AC_MSG_ERROR([ranlib not found])])
])

AS_IF([test "$host_os" = "linux-gnu"],[
    AC_CHECK_HEADERS([X11/Xlib.h GL/gl.h GL/glu.h], [],
        [AC_MSG_ERROR([Missing required X11/OpenGL development headers])])
])

# depending on the type of build, we may want to remap the file paths
SRCREMAP=${SRCREMAP:-}
CUSTOMPDBPATH=${CUSTOMPDBPATH:-}

AS_IF([test -n "$CUSTOMPDBPATH"], [ # alias override
    SRCREMAP="$CUSTOMPDBPATH"
])

AS_IF([test "$SRCREMAP" != "no" && test "$host_os" = "mingw32" && { test -n "$SRCREMAP" || test "x$enable_clang" = "xyes" ; }], [
    # so that the source folder can be mapped to a virtual drive like X:\ and be debugged under windows more easily

    # this seems to be the only way i can get visual studio to find the source paths automatically,
    # mounting the git source at X:\ and using -Wl,pdbsourcepath:'X:\'
    # this ends up turning a path like "/home/xyz/neosu/build-folder" into "X:\build-folder"

    # FIXME: non-portable realpath?
    rel_src_to_build_dirname="$(realpath --relative-to="$srcdir" "$PWD")"

    CXXFLAGS="$CXXFLAGS -ffile-prefix-map=\"\${abs_top_builddir}\"=\"${rel_src_to_build_dirname}\""
    CFLAGS="$CFLAGS -ffile-prefix-map=\"\${abs_top_builddir}\"=\"${rel_src_to_build_dirname}\""
    NEOSU_LDFLAGS="$NEOSU_LDFLAGS -ffile-prefix-map=\"\${abs_top_builddir}\"=\"${rel_src_to_build_dirname}\""
])

# this is currently extremely broken with mingw-gcc
AS_IF([test "$host_os" = "mingw32" && { test -n "$SRCREMAP" || test "x$enable_clang" = "xyes" ; } ], [
    cxxflags_save="$CXXFLAGS"
    cflags_save="$CFLAGS"
    AX_CHECK_COMPILE_FLAG([-gcodeview], [
        CXXFLAGS="-g $CXXFLAGS -gcodeview" # add some debug info too since its detached
        CFLAGS="-g $CFLAGS -gcodeview"
        AX_CHECK_LINK_FLAG([-Wl,--pdb=], [
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,--pdb="
        ])
        # speeds up link time on llvm-mingw, not crucial though
        AX_CHECK_COMPILE_FLAG([-gcodeview-ghash], [
            AX_CHECK_LINK_FLAG([-Wl,/debug:ghash], [
                CXXFLAGS="$CXXFLAGS -gcodeview-ghash"
                CFLAGS="$CFLAGS -gcodeview-ghash"
                NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,/debug:ghash"
            ])
        ])
        AX_CHECK_LINK_FLAG([-Wl,/pdbaltpath:%_PDB%], [
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,/pdbaltpath:%_PDB%"
        ])
        AS_IF([test "$SRCREMAP" != "no"], [
            AX_CHECK_LINK_FLAG([-Wl,/pdbsourcepath:], [
                NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,/pdbsourcepath:'${SRCREMAP:-X:\\}'"
            ])
        ])
    ])
])

AS_IF([test "x$enable_debug" = "xyes"], [
    AS_IF([test "x$with_mimalloc" != "xyes"], [with_mimalloc=no])
    AS_IF([test "$host_os" = "mingw32"],[
        # make the exe open with a console window
        AS_IF([test "x$enable_clang" = "xyes"], [
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,--subsystem,console:${win_subsystem_ver}"

            CXXFLAGS="$CXXFLAGS -g3 -Og"
            CFLAGS="$CFLAGS -g3 -Og"
        ], [
            NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -mconsole"
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -mconsole -Wl,--subsystem,console:${win_subsystem_ver}"

            CXXFLAGS="$CXXFLAGS -g3 -ggdb3 -gdwarf-5 -Og"
            CFLAGS="$CFLAGS -g3 -ggdb3 -gdwarf-5 -Og"
        ])
    ], [
        # don't remap paths, the source should correspond to the real source location on linux
        # non-mingw
        CXXFLAGS="$CXXFLAGS -g3 -ggdb3 -gdwarf-5 -Og"
        CFLAGS="$CFLAGS -g3 -ggdb3 -gdwarf-5 -Og"
    ])
    AS_IF([test -n "$WASM_CC"], [
        CXXFLAGS="$CXXFLAGS -gsource-map"
        CFLAGS="$CFLAGS -gsource-map"
        LDFLAGS="$LDFLAGS -gsource-map"
    ])

    ENABLE_DEBUG=$enable_debug
    NEOSU_CPPFLAGS="$NEOSU_CPPFLAGS -D_DEBUG"
    AS_IF([test -z "$CLANG" && test -z "$WASM_CC"], [
        NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -Wcast-align -Wpacked-not-aligned"
        # CPPFLAGS="$CPPFLAGS -D_GLIBCXX_DEBUG"
        # CXXFLAGS="$CXXFLAGS -D_GLIBCXX_DEBUG"
    ])
    # assertions breaks in a couple places due to calling malloc/free in the soloud mixer (inside SoundTouch)
    # hopefully it's not fatal to ignore...
    # AS_IF([test -n "$WASM_CC"], [
    #     NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -sASSERTIONS"
    #     NEOSU_LDFLAGS="$NEOSU_LDFLAGS -sASSERTIONS"
    # ])
    AC_SUBST([ENABLE_DEBUG])
    AS_IF([test "x$enable_strip" != "xyes"], [enable_strip=no]) # disable stripping unless its explicitly requested for debug (why tho?)
], [
    AS_IF([test "x$enable_optimize" != "xno"], [
        CPPFLAGS="$CPPFLAGS -DNDEBUG -D_NDEBUG"
        # put it at the start so -O3 is overrideable from the command line
        CXXFLAGS="-O3 $(printf '%s' "$CXXFLAGS" | sed 's/-O2//g')" # where do those -g -O2 flags even come from...
        CFLAGS="-O3 $(printf '%s' "$CFLAGS" | sed 's/-O2//g')"
        NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,-O1"

        AS_IF([test "x$enable_clang" != "xyes" && test -z "$WASM_CC"], [
            # -fipa-pta is GCC-only, skip for clang and emscripten
            CXXFLAGS="$CXXFLAGS -fipa-pta"
            CFLAGS="$CFLAGS -fipa-pta"
        ])
    ])
    AS_IF([test "x$enable_strip" != "xno"], [
        NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,-s" # strip flag
    ], [
        AS_IF([test -z "$WASM_CC"],[
            # add minimal debug info
            CXXFLAGS="-g $CXXFLAGS"
            CFLAGS="-g $CFLAGS"
        ])
    ])
    AS_IF([test "$host_os" = "mingw32"], [
        AS_IF([test "x$enable_clang" = "xyes"], [
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,--subsystem,windows:${win_subsystem_ver}"
        ], [
            NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -mwindows"
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -mwindows -Wl,--subsystem,windows:${win_subsystem_ver}"
        ])
    ])
])

AS_IF([test "x$enable_omp" = "xyes"], [
    AS_IF([test "x$enable_clang" = "xyes"], [
        NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -fopenmp-optimistic-collapse"
    ])
    NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -fopenmp"
    AC_SUBST([OPENMP], [1])
])

_using_gcc_lto=
AS_IF([test "x$enable_lto" != "xno"], [
    AX_CHECK_COMPILE_FLAG([-flto=thin], [
        # -fwhole-program-vtables causes LTO unit splitting issues with emscripten
        AS_IF([test -n "$WASM_CC"], [
            _lto_flags="-flto=thin -fno-fat-lto-objects"
        ], [
            _lto_flags="-flto=thin -fno-fat-lto-objects -fwhole-program-vtables"
        ])
        AX_CHECK_LINK_FLAG([$_lto_flags], [
            CXXFLAGS="$CXXFLAGS $_lto_flags"
            CFLAGS="$CFLAGS $_lto_flags"
            LDFLAGS="$LDFLAGS $_lto_flags"
            AS_IF([test -z "$MSYS2_BUILD"], [
                mkdir -p $PWD/.ltocache-clang
                AX_CHECK_LINK_FLAG([-Wl,--thinlto-cache-dir=$PWD/.ltocache-clang], [
                    LDFLAGS="$LDFLAGS -Wl,--thinlto-cache-dir=$PWD/.ltocache-clang"
                ])
            ])
        ])], [
        # gcc
        AX_CHECK_LINK_FLAG([-flto=auto -ffat-lto-objects -fuse-linker-plugin -fdevirtualize-at-ltrans], [
            _using_gcc_lto=1
            CXXFLAGS="$CXXFLAGS -flto=auto -ffat-lto-objects -fuse-linker-plugin -fdevirtualize-at-ltrans"
            CFLAGS="$CFLAGS -flto=auto -ffat-lto-objects -fuse-linker-plugin -fdevirtualize-at-ltrans"
            LDFLAGS="$LDFLAGS -flto=auto -ffat-lto-objects -fuse-linker-plugin -fdevirtualize-at-ltrans"
            AS_IF([test -z "$MSYS2_BUILD"], [
                mkdir -p $PWD/.ltocache-gcc
                AX_CHECK_LINK_FLAG([-flto-incremental=$PWD/.ltocache-gcc], [
                    CXXFLAGS="$CXXFLAGS -flto-incremental=$PWD/.ltocache-gcc"
                    CFLAGS="$CFLAGS -flto-incremental=$PWD/.ltocache-gcc"
                    LDFLAGS="$LDFLAGS -flto-incremental=$PWD/.ltocache-gcc"
                ])
            ])
        ])
    ])
    # this is hilariously slow, don't use it
    AS_IF([test "x$enable_lto" = "xultra"], [
        AX_CHECK_LINK_FLAG([-flto-partition=one], [
            NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -flto-partition=one"
            NEOSU_LDFLAGS="$NEOSU_LDFLAGS -flto-partition=one"
        ])
    ])
])

# check for -Wl,--build-id (makes some debugging tools work better)
AX_CHECK_LINK_FLAG([-Wl,--build-id], [
    LDFLAGS="$LDFLAGS -Wl,--build-id"
])

# check for -Wl,--icf=all (binary size opt)
AX_CHECK_LINK_FLAG([-Wl,--icf=all], [
    LDFLAGS="$LDFLAGS -Wl,--icf=all"
    # this is also recommended if icf is used
    AX_CHECK_COMPILE_FLAG([-faddrsig], [
        CXXFLAGS="$CXXFLAGS -faddrsig"
        CFLAGS="$CFLAGS -faddrsig"
    ])
])

# see: https://clang.llvm.org/docs/UsersManual.html#cmdoption-fstrict-vtable-pointers
AX_CHECK_COMPILE_FLAG([-fstrict-vtable-pointers], [
    # only for our code, this will probably break dependencies
    NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -fstrict-vtable-pointers"
])

# check for --enable-hardened capability (-fhardened)
AS_IF([test "x$enable_harden" != "xno"], [
    AX_CHECK_COMPILE_FLAG([-fhardened], [
        CXXFLAGS="$CXXFLAGS -fhardened"
        CFLAGS="$CFLAGS -fhardened"
        hardened_build=yes
    ], [
        _hardening_cflags="-Wformat -Wformat=2 -Wformat-security -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3 -D_GLIBCXX_ASSERTIONS -fstrict-flex-arrays=3 -fstack-clash-protection -fstack-protector-strong"
        AX_CHECK_COMPILE_FLAG([$_hardening_cflags], [
            CXXFLAGS="$CXXFLAGS $_hardening_cflags"
            CFLAGS="$CFLAGS $_hardening_cflags"
            hardened_build=yes
        ])
    ])
    # also check some more target-specific linker flags
    AX_CHECK_LINK_FLAG([-Wl,-z,noexecstack], [
        LDFLAGS="$LDFLAGS -Wl,-z,noexecstack"
    ])
    AX_CHECK_LINK_FLAG([-Wl,-z,relro -Wl,-z,now], [
        LDFLAGS="$LDFLAGS -Wl,-z,relro -Wl,-z,now"
    ])
    AX_CHECK_LINK_FLAG([-Wl,--as-needed -Wl,--no-copy-dt-needed-entries], [
        LDFLAGS="$LDFLAGS -Wl,--as-needed -Wl,--no-copy-dt-needed-entries"
    ])
    AS_IF([test "$host_os" = "mingw32" && test -z "$LLVMMINGW"], [
        # otherwise libssp-0.dll is needed
        AX_CHECK_LINK_FLAG([-Wl,--whole-archive,-Bstatic -l:libssp.a -Wl,--no-whole-archive,-Bdynamic], [
            LDFLAGS="$LDFLAGS -Wl,--whole-archive,-Bstatic -l:libssp.a -Wl,--no-whole-archive,-Bdynamic"
        ])
    ])
])

# common flags
NEOSU_CPPFLAGS="$NEOSU_CPPFLAGS"
NEOSU_CXXFLAGS="$NEOSU_CXXFLAGS -Wall -Wextra -Wno-sign-compare -Wunused-parameter -Wdeprecated-declarations -Wno-error=deprecated-declarations"
NEOSU_LDFLAGS="$NEOSU_LDFLAGS -Wl,--gc-sections" # remove unused sections
AC_SUBST([NEOSU_CPPFLAGS])
AC_SUBST([NEOSU_CXXFLAGS])
AC_SUBST([NEOSU_LDFLAGS])

# dont error out on some false positive warnings during LTO
AS_IF([test "x$enable_werror" != "xno"], [
    werror_flags_temp="-Werror"
    AX_CHECK_COMPILE_FLAG([-Wno-error=stringop-overflow -Wno-error=maybe-uninitialized], [
        werror_flags_temp="$werror_flags_temp -Wno-error=stringop-overflow -Wno-error=maybe-uninitialized"
    ])
    AX_CHECK_COMPILE_FLAG([-Wno-error=alloc-size-larger-than=], [
        werror_flags_temp="$werror_flags_temp -Wno-error=alloc-size-larger-than="
    ])
    AX_CHECK_COMPILE_FLAG([-Wno-error=format-y2k], [
        werror_flags_temp="$werror_flags_temp -Wno-error=format-y2k"
    ])
    AS_IF([test -n "$WASM_CC"], [
        # not exactly sure why the above tests pass but then the actual compilation fails due to -Werror=unknown-warning-option
        AX_CHECK_COMPILE_FLAG([-Wno-unknown-warning-option], [
            werror_flags_temp="$werror_flags_temp -Wno-unknown-warning-option"
        ])
    ])
    NEOSU_CXXFLAGS="$werror_flags_temp $NEOSU_CXXFLAGS"
])

# sanity check for pre-bundled assets and required files before checking for external dependencies
AC_MSG_CHECKING([for src/Makefile.sources])
AS_IF([test -f "$srcdir/src/Makefile.sources"], [
    AC_MSG_RESULT([yes])
], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([src/Makefile.sources not found. Run ./autogen.sh first.])
])

ASSETS_DIR=assets
AC_SUBST([ASSETS_DIR])

AUDIO_CFLAGS=""
AUDIO_LIBS=""

AC_SUBST([AUDIO_CFLAGS])
AC_SUBST([AUDIO_LIBS])

# the source files are also added through autogen.sh to Makefile.sources
BUNDLED_INCLUDES="-I\$(srcdir)/libraries -I\$(srcdir)/libraries/delegatesv2 -I\$(srcdir)/libraries/unordered_dense"
AC_SUBST([BUNDLED_INCLUDES])

AC_MSG_CHECKING([for shaders in $srcdir/$ASSETS_DIR/shaders])
AS_IF([test -f "$srcdir/$ASSETS_DIR/shaders/GL_slider_f.glsl"], [
    AC_MSG_RESULT([yes])
], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([neosu shader resources are missing!])
])

AC_MSG_CHECKING([for fonts in $srcdir/$ASSETS_DIR/fonts])
AS_IF([test -d "$srcdir/$ASSETS_DIR/fonts"], [
    AC_MSG_RESULT([yes])
], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([neosu fonts are missing!])
])

AC_MSG_CHECKING([for default material assets in $srcdir/$ASSETS_DIR/materials])
AS_IF([test -d "$srcdir/$ASSETS_DIR/materials"], [
    AC_MSG_RESULT([yes])
], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([neosu material assets are missing!])
])

AC_DEFUN([PKG_CHECK_AUTO], [
    AS_IF([test "x$enable_static" = "xyes"],
        [PKG_CHECK_MODULES_STATIC([$1], [$2], [$3], [$4])],
        [PKG_CHECK_MODULES([$1], [$2], [$3], [$4])])
])

AC_SUBST([DEPS_PREFIX], ['${abs_top_builddir}/build/deps'])
AC_SUBST([DEPS_CACHE], ['${abs_top_srcdir}/build-aux/cache'])
AC_SUBST([TEMP_WINEPFX], ['${abs_top_srcdir}/build-aux/.tmpwinepfx'])
AC_SUBST([BUILD_MISCDIR], ['${abs_top_srcdir}/build-aux/misc'])
AC_SUBST([bindir], [$bindir-$host_cpu])

AS_IF([test "x$use_opengl" = "xyes" || test "x$use_opengles32" = "xyes"], [
    AS_IF([test "$SYSNAME" = "linux"], [
        PKG_CHECK_AUTO([GLU], [glu], [have_glu=yes], [AC_MSG_ERROR([Missing glu libs, required for OpenGL])])
        PKG_CHECK_AUTO([GL], [gl], [have_gl=yes], [AC_MSG_ERROR([Missing gl libs, required for OpenGL])])
        PKG_CHECK_AUTO([X11], [x11], [have_x11=yes], [AC_MSG_ERROR([Missing x11 libs, required for OpenGL on Linux])])
        PKG_CHECK_AUTO([XI], [xi], [have_xi=yes], [AC_MSG_ERROR([Missing xi (xinput) libs, required for OpenGL on Linux])])
        PKG_CHECK_AUTO([XRANDR], [xrandr], [have_xrandr=yes], [AC_MSG_ERROR([Missing xrandr libs, required for Linux])])
        PKG_CHECK_AUTO([EGL], [egl], [have_egl=yes], [AC_MSG_ERROR([Missing egl libs, required for OpenGL on Linux])])
    ], [
        have_glu="not required"
        have_gl="not required"
        have_x11="not required"
        have_xi="not required"
        have_xrandr="not required"
        have_egl="not required"
    ])
])

AS_IF([test "x$use_dx11" = "xyes"], [
    AS_IF([test "$SYSNAME" = "linux"], [
        PKG_CHECK_AUTO([DXGI], [dxvk-dxgi], [have_dxgi=yes], [have_dxgi=no])
        PKG_CHECK_AUTO([D3D11], [dxvk-d3d11], [have_d3d11=yes], [have_d3d11=no])
        PKG_CHECK_AUTO([VKD3D], [libvkd3d-utils], [have_vkd3d=yes], [have_vkd3d=no])
        AS_IF([test "x$have_dxgi" != "xyes" || test "x$have_d3d11" != "xyes" || test "x$have_vkd3d" != "xyes"], [
            AC_MSG_ERROR([Missing dxvk-native or libvkd3d-utils libraries for D3D11 on Linux])
        ])
        FORCE_SDL_DYNAMIC=1 # dxvk double-loads the wrong library version if we statically link to sdl, so until i (if ever) build dxvk in-tree
                            # it needs to be forced dynamic
    ],[
        AC_SUBST([D3D11_CFLAGS], [""])
        AC_SUBST([D3D11_LIBS], [[""]])
        AC_SUBST([DXGI_CFLAGS], [""])
        AC_SUBST([DXGI_LIBS], [[""]])
        AC_SUBST([VKD3D_CFLAGS], [""])
        AC_SUBST([VKD3D_LIBS], [[""]])
    ])
])

SDL3_MIN_VERSION=3.4.0
SDL3_BUILD_VERSION=46e553a44c145abe4f0453c6acd6d125a28eb7e0 # git
FREETYPE2_MIN_VERSION=2.10.0
FREETYPE2_BUILD_VERSION=2.14.1
LIBJPEG_MIN_VERSION=3.0.0
LIBJPEG_BUILD_VERSION=3.1.3
LIBPNG_MIN_VERSION=1.6.38
LIBPNG_BUILD_VERSION=1.6.54
ZLIB_MIN_VERSION=1.3
ZLIB_BUILD_VERSION=2.3.2 # zlib-ng
BZIP2_MIN_VERSION=1.0.8
BZIP2_BUILD_VERSION=1.0.8
FMT_MIN_VERSION=11.0.0
FMT_BUILD_VERSION=0e078f6ed0624be8babc43bd145371d9f3a08aab
LZMA_MIN_VERSION=5.4.7
LZMA_BUILD_VERSION=5.8.2
LIBARCHIVE_MIN_VERSION=3.6.2
LIBARCHIVE_BUILD_VERSION=3.8.5
LIBACL_MIN_VERSION=2.3.1
LIBACL_BUILD_VERSION=2.3.2
CURL_MIN_VERSION=8.16.0 # due to websocket things, only very recent curl works
CURL_BUILD_VERSION=8.18.0
MBEDTLS_BUILD_VERSION=4.0.0 # for WASM SSL support
OPENSSL_MIN_VERSION=3.0.0 # only for --enable-system-deps
MPG123_MIN_VERSION=1.30.0
MPG123_BUILD_VERSION=a06133928e6518bd65314c9cea12ccb5588703e9 # git mirror, latest as of Jan 13th, 2026 (1.33.5)
SPDLOG_MIN_VERSION=1.15.0
SPDLOG_BUILD_VERSION=1.17.0
NSYNC_MIN_VERSION=1.29.2
NSYNC_BUILD_VERSION=1.30.0
SIMDUTF_MIN_VERSION=7.0.0
SIMDUTF_BUILD_VERSION=8.0.0 # fast and correct utf conversion utilities
CTRE_MIN_VERSION=3.10.0 # regex support that doesn't have exceptions FORCED enabled
CTRE_BUILD_VERSION=6225211806c48230e5d17a1e555ef69e7325051c # git as of Jan 28th, 2026

# always build these from source if we want to use them
AC_SUBST([SOLOUD_VERSION], [800cb5eb24756eac79fab0803d8704d1c5fe28c5]) # neoloud fork
#AC_SUBST([SOLOUD_VERSION], [cb174f53804d8ba3fc81a2dfa49879103fbbcb2e])
AC_SUBST([SOLOUD_PREFIX],[$DEPS_PREFIX])
AC_SUBST([SOUNDTOUCH_VERSION], [b8e2a5f3f04483691962ab1a36ffa166abdcd675])
AC_SUBST([SOUNDTOUCH_PREFIX],[$DEPS_PREFIX])
AC_SUBST([MIMALLOC_VERSION], [v3.2.7])
AC_SUBST([MIMALLOC_PREFIX],[$DEPS_PREFIX])
AC_SUBST([GLM_VERSION], [1.0.3])

# pre-built, used for soloud
# NOTE: the links are not stable across architectures, so this is more of a symbolic version than anything
# (needs manual url changes in Makefile.am)
AC_SUBST([FFMPEG_VERSION], [8.0])

AC_SUBST([DISCORDSDK_VERSION], [2.5.6]) # pre-built

AC_SUBST([BASS_VERSION], [20260208_202602]) # just mark the date so we can invalidate the cache if necessary

AS_IF([test "$SYSNAME" = "linux" || test "$SYSNAME" = "macos"], [
    AS_IF([test "x$enable_system_deps" != "xno"], [
        PKG_CHECK_AUTO([CURL], [libcurl >= $CURL_MIN_VERSION], [have_curl=yes], [have_curl=no])
        PKG_CHECK_AUTO([LIBACL], [libacl >= $LIBACL_MIN_VERSION], [have_libacl=yes], [have_libacl=no])
    ])
], [
    AS_IF([test "$SYSNAME" = "windows"], [
        # Windows-specific libraries
        EXTRA_WIN_LDFLAGS="-lcomctl32"
        AC_SUBST([EXTRA_WIN_LDFLAGS])
    ])
    have_curl="no"
    have_libacl="not required"
])

# TODO: add all deps and transitive deps (fun!)
AS_IF([test "x$enable_system_deps" != "xno"], [
    PKG_CHECK_AUTO([SDL3], [sdl3 >= $SDL3_MIN_VERSION], [have_sdl3=yes], [have_sdl3=no])
    PKG_CHECK_AUTO([LIBJPEG], [libturbojpeg >= $LIBJPEG_MIN_VERSION], [have_libjpeg=yes], [have_libjpeg=no])
    PKG_CHECK_AUTO([LIBPNG], [libpng >= $LIBPNG_MIN_VERSION], [have_libpng=yes], [have_libpng=no])
    PKG_CHECK_AUTO([ZLIB], [zlib >= $ZLIB_MIN_VERSION], [have_zlib=yes], [have_zlib=no])
    PKG_CHECK_AUTO([BZIP2], [bzip2 >= $BZIP2_MIN_VERSION], [have_bzip2=yes], [have_bzip2=no])
    PKG_CHECK_AUTO([FREETYPE2], [freetype2 >= $FREETYPE2_MIN_VERSION], [have_freetype2=yes], [have_freetype2=no])
    PKG_CHECK_AUTO([FMT], [fmt >= $FMT_MIN_VERSION], [have_fmt=yes], [have_fmt=no])
    PKG_CHECK_AUTO([LZMA], [liblzma >= $LZMA_MIN_VERSION], [have_liblzma=yes], [have_liblzma=no])
    PKG_CHECK_AUTO([LIBARCHIVE], [libarchive >= $LIBARCHIVE_MIN_VERSION], [have_libarchive=yes], [have_libarchive=no])
    PKG_CHECK_AUTO([SIMDUTF], [simdutf >= $SIMDUTF_MIN_VERSION], [have_simdutf=yes], [have_simdutf=no])
    PKG_CHECK_AUTO([CTRE], [ctre >= $CTRE_MIN_VERSION], [have_ctre=yes], [have_ctre=no])
    # check for standalone openssl if we have system curl
    AS_IF([test "x$have_curl" = "xyes"], [
        PKG_CHECK_AUTO([OPENSSL], [openssl >= $OPENSSL_MIN_VERSION], [have_openssl=yes], [have_openssl=no])
    ])
    AS_IF([test "$with_soloud" = "yes"], [
        PKG_CHECK_AUTO([MPG123], [libmpg123 >= $MPG123_MIN_VERSION], [have_mpg123=yes], [have_mpg123=no])
    ])
    PKG_CHECK_AUTO([SPDLOG], [spdlog >= $SPDLOG_MIN_VERSION], [have_spdlog=yes], [have_spdlog=no])
    AC_CHECK_HEADERS([glm/vec3.hpp], [have_glm=yes], [have_glm=no])
    AS_IF([test -z "$ubercompat" && test "x$with_nsync" != "xno"], [
        AC_SEARCH_LIBS([nsync_mu_init], [nsync_cpp], [have_nsync=yes], [have_nsync=no])
    ])
])

AS_IF([test -z "$ubercompat" && test "x$with_nsync" != "xno"], [
    AC_DEFINE([USE_NSYNC], [1], [Defined if nsync sync primitives are available.])
], [
    have_nsync=unsupported
])

AS_IF([test -n "$ubercompat"], [
    AC_SEARCH_LIBS(_set_abort_behavior, [], [], [msvcrt_build=yes])
    AS_IF([test "x$msvcrt_build" = "xyes"], [
        AC_DEFINE([MCENGINE_PLATFORM_WINDOWS_MSVCRT], [1], [Defined if building against the MSVCRT library instead of UCRT.])
    ])
])

AS_IF([test "$host_os" = "linux-gnu"], [
    # somehow this is advertised as being available on mingw-gcc, and then when you go to use it it just prints a bunch of empty lines
    _have_stdstacktrace=
    AC_SEARCH_LIBS([__glibcxx_backtrace_simple], [stdc++exp], [_have_stdstacktrace=yes], [_have_stdstacktrace=no])
    AS_IF([test "$_have_stdstacktrace" = "yes"], [
        # AC_SEARCH_LIBS also automatically adds -lstdc++exp to linker flags if found
        AC_DEFINE([MCENGINE_HAVE_STDSTACKTRACE], [1], [Defined if <stacktrace> is available to print execution backtraces.])
    ])
])

AS_IF([test "x$have_sdl3" != "xyes"], [have_sdl3="build from source"])
SDL3_VERSION=$SDL3_BUILD_VERSION
SDL3_PREFIX="${DEPS_PREFIX}"
AS_IF([test "$STATICBUILD" = "yes" && test -z "$FORCE_SDL_DYNAMIC"], [
    SDL3_STATIC=1
])
AC_SUBST([SDL3_VERSION])
AC_SUBST([SDL3_PREFIX])
AC_SUBST([SDL3_STATIC])

AS_IF([test "x$have_libjpeg" != "xyes"], [
    have_libjpeg="build from source"
    AC_PROG_NASM
])

LIBJPEG_VERSION=$LIBJPEG_BUILD_VERSION
LIBJPEG_PREFIX="${DEPS_PREFIX}"
AC_SUBST([LIBJPEG_VERSION])
AC_SUBST([LIBJPEG_PREFIX])

AS_IF([test "x$have_libpng" != "xyes"], [have_libpng="build from source"])
LIBPNG_VERSION=$LIBPNG_BUILD_VERSION
LIBPNG_PREFIX="${DEPS_PREFIX}"
AC_SUBST([LIBPNG_VERSION])
AC_SUBST([LIBPNG_PREFIX])

AS_IF([test "x$have_bzip2" != "xyes"], [have_bzip2="build from source"])
BZIP2_VERSION=$BZIP2_BUILD_VERSION
BZIP2_PREFIX="${DEPS_PREFIX}"
AC_SUBST([BZIP2_VERSION])
AC_SUBST([BZIP2_PREFIX])

AS_IF([test "x$have_zlib" != "xyes"], [have_zlib="build from source"])
ZLIB_VERSION=$ZLIB_BUILD_VERSION
ZLIB_PREFIX="${DEPS_PREFIX}"
AC_SUBST([ZLIB_VERSION])
AC_SUBST([ZLIB_PREFIX])

AS_IF([test "x$have_freetype2" != "xyes"], [have_freetype2="build from source"])
FREETYPE2_VERSION=$FREETYPE2_BUILD_VERSION
FREETYPE2_PREFIX="${DEPS_PREFIX}"
AC_SUBST([FREETYPE2_VERSION])
AC_SUBST([FREETYPE2_PREFIX])

AS_IF([test "x$have_fmt" != "xyes"], [have_fmt="build from source"])
FMT_VERSION=$FMT_BUILD_VERSION
FMT_PREFIX="${DEPS_PREFIX}"
AC_SUBST([FMT_VERSION])
AC_SUBST([FMT_PREFIX])

AS_IF([test "x$have_liblzma" != "xyes"], [have_liblzma="build from source"])
LZMA_VERSION=$LZMA_BUILD_VERSION
LZMA_PREFIX="${DEPS_PREFIX}"
AC_SUBST([LZMA_VERSION])
AC_SUBST([LZMA_PREFIX])

AS_IF([test "x$have_libarchive" != "xyes"], [have_libarchive="build from source"])
LIBARCHIVE_VERSION=$LIBARCHIVE_BUILD_VERSION
LIBARCHIVE_PREFIX="${DEPS_PREFIX}"
AC_SUBST([LIBARCHIVE_VERSION])
AC_SUBST([LIBARCHIVE_PREFIX])

AS_IF([test "x$have_simdutf" != "xyes"], [have_simdutf="build from source"])
SIMDUTF_VERSION=$SIMDUTF_BUILD_VERSION
SIMDUTF_PREFIX="${DEPS_PREFIX}"
AC_SUBST([SIMDUTF_VERSION])
AC_SUBST([SIMDUTF_PREFIX])

AS_IF([test "x$have_ctre" != "xyes"], [have_ctre="build from source"])
CTRE_VERSION=$CTRE_BUILD_VERSION
CTRE_PREFIX="${DEPS_PREFIX}"
AC_SUBST([CTRE_VERSION])
AC_SUBST([CTRE_PREFIX])

AS_IF([test "x$have_libarchive" = "xbuild from source" &&
    { test "x$have_libacl" != "xyes" && test "x$have_libacl" != "xnot required"; }], [have_libacl="build from source"])
LIBACL_VERSION=$LIBACL_BUILD_VERSION
LIBACL_PREFIX="${DEPS_PREFIX}"
AC_SUBST([LIBACL_VERSION])
AC_SUBST([LIBACL_PREFIX])

AS_IF([test "x$have_curl" != "xyes"], [
    AS_IF([test -n "$WASM_CC"], [
        # WASM doesn't use curl (browser networking goes through emscripten fetch API)
        have_curl="not required"
        have_openssl="not required"
    ], [
        # other platforms download prebuilt curl with OpenSSL
        have_curl="download"
    ])
])
CURL_VERSION=$CURL_BUILD_VERSION
CURL_PREFIX="${DEPS_PREFIX}"
AC_SUBST([CURL_VERSION])
AC_SUBST([CURL_PREFIX])
MBEDTLS_VERSION=$MBEDTLS_BUILD_VERSION
AC_SUBST([MBEDTLS_VERSION])

# if we are using the prebuilt curl (with bundled OpenSSL), we don't need libcrypto from the system
AS_IF([test "x$have_curl" = "xdownload" || test "x$have_openssl" = "xyes"], [
    AC_DEFINE([USE_OPENSSL], [1], [Defined if OpenSSL cryptographic primitives are available.])
])

AS_IF([test "$with_soloud" = "yes"], [
    AS_IF([test "x$have_mpg123" != "xyes"], [have_mpg123="build from source"])
], [
    have_mpg123="not required"
])
MPG123_VERSION=$MPG123_BUILD_VERSION
MPG123_PREFIX="${DEPS_PREFIX}"
AC_SUBST([MPG123_VERSION])
AC_SUBST([MPG123_PREFIX])

AS_IF([test "x$have_spdlog" != "xyes"], [have_spdlog="build from source"])
SPDLOG_VERSION=$SPDLOG_BUILD_VERSION
SPDLOG_PREFIX="${DEPS_PREFIX}"
AC_SUBST([SPDLOG_VERSION])
AC_SUBST([SPDLOG_PREFIX])

AS_IF([test "x$have_glm" != "xyes"], [have_glm="build from source"])
AC_SUBST([GLM_PREFIX],[$DEPS_PREFIX])
AC_SUBST([GLM_CFLAGS], [""])

AS_IF([test "x$have_nsync" != "xyes" && test -z "$ubercompat" && test "x$with_nsync" != "xno"], [have_nsync="build from source"])
NSYNC_VERSION=$NSYNC_BUILD_VERSION
NSYNC_PREFIX="${DEPS_PREFIX}"
AC_SUBST([NSYNC_VERSION])
AC_SUBST([NSYNC_PREFIX])

AM_CONDITIONAL([BUILD_SDL3], [test "x$have_sdl3" != "xyes"])
AM_CONDITIONAL([BUILD_LIBJPEG], [test "x$have_libjpeg" != "xyes"])
AM_CONDITIONAL([BUILD_LIBPNG], [test "x$have_libpng" != "xyes"])
AM_CONDITIONAL([BUILD_BZIP2], [test "x$have_bzip2" != "xyes"])
AM_CONDITIONAL([BUILD_ZLIB], [test "x$have_zlib" != "xyes"])
AM_CONDITIONAL([BUILD_FREETYPE2], [test "x$have_freetype2" != "xyes"])
AM_CONDITIONAL([BUILD_FMT], [test "x$have_fmt" != "xyes"])
AM_CONDITIONAL([BUILD_LZMA], [test "x$have_liblzma" != "xyes"])
AM_CONDITIONAL([BUILD_LIBARCHIVE], [test "x$have_libarchive" != "xyes"])
AM_CONDITIONAL([BUILD_SIMDUTF], [test "x$have_simdutf" != "xyes"])
AM_CONDITIONAL([BUILD_CTRE], [test "x$have_ctre" != "xyes"])
AM_CONDITIONAL([BUILD_LIBACL], [test "x$have_libacl" = "xbuild from source"])
AM_CONDITIONAL([BUILD_CURL], [test "x$have_curl" != "xyes" && test "x$have_curl" != "xnot required"])
AM_CONDITIONAL([BUILD_MPG123], [test "$with_soloud" = "yes" && test "x$have_mpg123" != "xyes"])
AM_CONDITIONAL([BUILD_SPDLOG], [test "x$have_spdlog" != "xyes"])
AM_CONDITIONAL([BUILD_NSYNC], [test "x$have_nsync" = "xbuild from source"])
AM_CONDITIONAL([BUILD_GLM], [test "x$have_glm" != "xyes"])
AM_CONDITIONAL([BUILD_MIMALLOC], [{ test "x$enable_debug" != "xyes" || test "x$with_mimalloc" = "xyes"; } &&
                                { { test "$SYSNAME" = "windows" && test "$host_cpu" = "i686" && test "x$with_mimalloc" = "xyes" ; } ||
                                  { test "x$with_mimalloc" != "xno" ; };
                                }]) # default off for windows 32bit for compatibility

AM_CONDITIONAL([USE_BASS], [test "$with_bass" = "yes"])
AM_CONDITIONAL([USE_SOLOUD], [test "$with_soloud" = "yes"])
AM_CONDITIONAL([USE_GL], [test "$use_opengl" = "yes" || test "$use_opengles32" = "yes"])
AM_CONDITIONAL([USE_OPENGL], [test "$use_opengl" = "yes"])
AM_CONDITIONAL([USE_OPENGLES], [test "$use_opengles32" = "yes"])
AM_CONDITIONAL([USE_DX11], [test "$use_dx11" = "yes"])
AM_CONDITIONAL([USE_SDLGPU], [test "$use_sdlgpu" = "yes"])
AM_CONDITIONAL([DOWNLOAD_DXC], [test -z "$DXC" && test "$host_os" = "mingw32"])
# this just copies some libraries on installation
AM_CONDITIONAL([USE_DISCORDSDK], [test "$with_discord" = "yes"])
AM_CONDITIONAL([WIN_PLATFORM], [test "$SYSNAME" = "windows"])
AM_CONDITIONAL([WIN32_PLATFORM], [test "$SYSNAME" = "windows" && test "$host_cpu" = "i686"])
AM_CONDITIONAL([MACOS_PLATFORM], [test "$SYSNAME" = "macos"])
AM_CONDITIONAL([LINUX_PLATFORM], [test "$SYSNAME" = "linux"])
AM_CONDITIONAL([WASM_PLATFORM], [test -n "$WASM_CC"])
AM_CONDITIONAL([CLANG_BUILD], [test "$CLANG" = "yes"])

AM_CONDITIONAL([STRIPBINS], [test "x$enable_strip" != "xno" && ! { test "$SYSNAME" = "windows" && test "$host_cpu" = "i686" ; } ])

# to avoid building pch files redundantly in CI (not really worth it)
AS_IF([test "$CI" = "true" || test x"$enable_pch" = "xno"], [using_pch=no], [using_pch=yes])
AM_CONDITIONAL([BUILDING_IN_CI], [test "$CI" = "true"])
AM_CONDITIONAL([BUILD_PCH], [test "$using_pch" = "yes"])

# copy winpthread dll to install directory
AS_IF([test "$SYSNAME" = "windows" && test "x$enable_static" != "xyes"], [
    WINSHARED_DLLS="$($CC -print-search-dirs | grep '^programs:' | sed 's/programs: =//' | tr ':' '\n' | while read dir; do
                        if test -n "$dir"; then
                            find "$dir" -name "libwinpthread-1.dll" 2>/dev/null
                        fi
                    done | head -1)"
])

AC_SUBST([WINSHARED_DLLS])
AC_SUBST([ubercompat])
AM_CONDITIONAL([WINSHARED_DLLS_NEEDED], [test -n "$WINSHARED_DLLS"])
AM_CONDITIONAL([BUILD_WINPTHREAD_NOVERSION], 
    [test "$host_os" = "mingw32" && test "x$enable_static" = "xyes"])

AM_CONDITIONAL([AUDIO_SHORTCUTS], [test -n "$make_audio_shortcuts" && test -n "$WINE" && test "$SYSNAME" = "windows"])
AM_CONDITIONAL([BUNDLE_BEATMAPS], [test -n "$WASM_CC" && test "x$enable_redistributable" = "xno"])

# output makefile and build timestamp
AC_CONFIG_FILES([Makefile src/build_timestamp.h])

AC_OUTPUT

AC_MSG_NOTICE([
neosu build configuration:
    Build target:       ${target}
    Install prefix:     ${prefix}
    Renderers:          ${rendbackendlist}
    Preprocessor flags: ${CPPFLAGS}
    ccache:             ${CCACHE-no}
    clang:              ${CLANG-no}
    mold:               ${using_mold-no}
    PCH:                ${using_pch}
    C++ compiler:       ${CXX}
    C++ flags:          ${CXXFLAGS}
    C compiler:         ${CC}
    C flags:            ${CFLAGS}
    Linker flags:       ${LDFLAGS}
    mimalloc:           ${with_mimalloc-yes}
    nsync:              ${with_nsync-yes}
    BASS:               ${with_bass-no}
    SoLoud:             ${with_soloud-no}
    Discord:            ${with_discord-yes}
    IME enabled:        ${enable_ime-yes}
    Hardened build:     ${hardened_build-no}
    Redistrib. build:   ${enable_redistributable-yes}
    LTO:                ${enable_lto-auto}
    OpenMP:             ${enable_omp-no}
    Debug build:        ${enable_debug-no}
    ASan build:         ${enable_asan-no}
    TSan build:         ${enable_tsan-no}
    Native build:       ${enable_native-no}
    System deps:        ${enable_system_deps-yes}
    Static linking:     ${enable_static-no}
    Stripped build:     ${enable_strip-yes}

Dependencies:
    libX11              ${have_x11-no}
    libXi (XInput)      ${have_xi-no}
    xrandr              ${have_xrandr-no}
    libGL               ${have_gl-no}
    libGLU              ${have_glu-no}
    libEGL              ${have_egl-no}
    sdl3:               ${have_sdl3-build from source}
    libjpeg:            ${have_libjpeg-build from source}
    libpng:             ${have_libpng-build from source}
    bzip2:              ${have_bzip2-build from source}
    zlib:               ${have_zlib-build from source}
    freetype2:          ${have_freetype2-build from source}
    fmt:                ${have_fmt-build from source}
    curl:               ${have_curl-download}
    openssl:            ${have_openssl-download}
    liblzma:            ${have_liblzma-build from source}
    libarchive:         ${have_libarchive-build from source}
    simdutf:            ${have_simdutf-build from source}
    ctre:               ${have_ctre-build from source}
    libacl:             ${have_libacl-build from source}
    mpg123:             ${have_mpg123-build from source}
    spdlog:             ${have_spdlog-build from source}
    nsync:              ${have_nsync-build from source}
    glm (math)          ${have_glm-build from source}
])
