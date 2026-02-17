cmake_minimum_required(VERSION 3.20)
project(neomod-superbuild)

# MSVC specific configuration
if(NOT MSVC)
    message(FATAL_ERROR "This CMake configuration is specifically for MSVC")
endif()

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

find_program(CCACHE ccache)
if(NOT CCACHE STREQUAL "CCACHE-NOTFOUND")
    set(CMAKE_C_COMPILER_LAUNCHER ccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(WITH_LIVEPP "Enable LivePP support" OFF)

# Use static runtime libraries
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Parallel build configuration
include(ProcessorCount)
ProcessorCount(CPU_COUNT)
if(CPU_COUNT EQUAL 0)
    set(CPU_COUNT 4)
endif()

if(WINEBUILD)
    set(CMAKE_COMPAT_GENERATOR "Ninja")
else()
    set(CMAKE_COMPAT_GENERATOR "${CMAKE_GENERATOR}")
endif()

# Shared dependency cache and install dirs
set(DEPS_CACHE "${CMAKE_SOURCE_DIR}/depcache")
set(DEPS_PREFIX "${CMAKE_BINARY_DIR}/deps")
set(DEPS_INSTALL_DIR "${DEPS_PREFIX}/install")

# Create directories
file(MAKE_DIRECTORY "${DEPS_CACHE}")
file(MAKE_DIRECTORY "${DEPS_PREFIX}")
file(MAKE_DIRECTORY "${DEPS_INSTALL_DIR}")

# Load dependency information
include(${CMAKE_SOURCE_DIR}/dependencies.cmake)

# for libjpeg-turbo, and bundling binary dependencies
find_program(NASM nasm HINTS "C:/Program Files/NASM" REQUIRED)

include(ExternalProject)
find_package(PkgConfig REQUIRED)

# Set up pkg-config environment for finding our built dependencies
set(ENV{PKG_CONFIG_PATH} "${DEPS_INSTALL_DIR}/lib/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${DEPS_INSTALL_DIR}/lib/pkgconfig")
set(ENV{PKG_CONFIG_MSVC_SYNTAX} 1)
set(PKG_CONFIG_ARGN "--msvc-syntax")

# Compiler flags
# Strip default cmake exception handling flags
string(REGEX REPLACE "/EH[^ ]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(COMMON_C_FLAGS "/utf-8" "/D_HAS_EXCEPTIONS=0" "/D_UNICODE" "/DUNICODE" "/EHs-c-")
set(COMMON_CXX_FLAGS "/utf-8" "/Zc:preprocessor" "/Zc:__cplusplus" "/GR" "/D_HAS_EXCEPTIONS=0" "/DSOLOUD_NO_ASSERTS" "/D_UNICODE" "/DUNICODE" "/EHs-c-")
set(DEBUG_FLAGS "/Zi" "/Od" "/RTC1")
set(RELEASE_FLAGS "/Zi" "/O2" "/Oi" "/Oy" "/Gw" "/GS-" "/Gy")

# Link flags
set(DEBUG_LINK_FLAGS "/DEBUG:FULL")
set(RELEASE_LINK_FLAGS "/DEBUG:FULL")

# Convert flag lists to strings for external projects
string(JOIN " " COMMON_C_FLAGS_STR ${COMMON_C_FLAGS})
string(JOIN " " COMMON_CXX_FLAGS_STR ${COMMON_CXX_FLAGS})
string(JOIN " " DEBUG_FLAGS_STR ${DEBUG_FLAGS})
string(JOIN " " RELEASE_FLAGS_STR ${RELEASE_FLAGS})
string(JOIN " " DEBUG_LINK_FLAGS_STR ${DEBUG_LINK_FLAGS})
string(JOIN " " RELEASE_LINK_FLAGS_STR ${RELEASE_LINK_FLAGS})

# Common external project arguments
set(EXTERNAL_PROJECT_CMAKE_ARGS
    -DCMAKE_SYSTEM_NAME=Windows
    -DCMAKE_SYSTEM_PROCESSOR=AMD64
    -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_DIR}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
    -DCMAKE_PREFIX_PATH=${DEPS_INSTALL_DIR}
    -DCMAKE_FIND_ROOT_PATH=${DEPS_INSTALL_DIR}
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
    -DCMAKE_C_FLAGS=${COMMON_C_FLAGS_STR}
    -DCMAKE_CXX_FLAGS=${COMMON_CXX_FLAGS_STR}
    -DCMAKE_C_FLAGS_DEBUG=${DEBUG_FLAGS_STR}
    -DCMAKE_CXX_FLAGS_DEBUG=${DEBUG_FLAGS_STR}
    -DCMAKE_C_FLAGS_RELEASE=${RELEASE_FLAGS_STR}
    -DCMAKE_CXX_FLAGS_RELEASE=${RELEASE_FLAGS_STR}
    -DCMAKE_EXE_LINKER_FLAGS_DEBUG=${DEBUG_LINK_FLAGS_STR}
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE=${RELEASE_LINK_FLAGS_STR}
    -DCMAKE_SHARED_LINKER_FLAGS_DEBUG=${DEBUG_LINK_FLAGS_STR}
    -DCMAKE_SHARED_LINKER_FLAGS_RELEASE=${RELEASE_LINK_FLAGS_STR}
    -DCMAKE_STATIC_LINKER_FLAGS_RELEASE=${RELEASE_LINK_FLAGS_STR}
    -DBZIP2_INCLUDE_DIR=${DEPS_INSTALL_DIR}/include # bzip2's .pc file is literally just broken
    -DBZIP2_LIBRARIES=${DEPS_INSTALL_DIR}/lib/bz2_static.lib
    -DBZIP2_LIBRARY_RELEASE=${DEPS_INSTALL_DIR}/lib/bz2_static.lib
    -DBZIP2_LIBRARY_DEBUG=${DEPS_INSTALL_DIR}/lib/bz2_static.lib
)

# Build dependencies - order matters due to interdependencies
add_external_project(zlib
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DZLIB_COMPAT=ON
        -DZLIB_ENABLE_TESTS=OFF
        -DZLIBNG_ENABLE_TESTS=OFF
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(bzip2
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DENABLE_STATIC_RUNTIME=ON
        -DENABLE_STATIC_LIB=ON
        -DENABLE_SHARED_LIB=OFF
        -DENABLE_LIB_ONLY=ON
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(libjpeg
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DENABLE_STATIC=ON
        -DENABLE_SHARED=OFF
        -DWITH_JPEG8=ON
        -DWITH_SIMD=ON
        -DREQUIRE_SIMD=OFF
        -DCMAKE_ASM_NASM_FLAGS="-DPIC"
        -DCMAKE_ASM_NASM_OBJECT_FORMAT=win64
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(libpng
    DEPENDS zlib_external
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DPNG_SHARED=OFF
        -DPNG_STATIC=ON
        -DPNG_TESTS=OFF
        -DPNG_TOOLS=OFF
        -DMINGW=ON
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(freetype
    DEPENDS zlib_external libpng_external bzip2_external
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DFT_DISABLE_HARFBUZZ=ON
        -DFT_DISABLE_BROTLI=ON
        -DFT_REQUIRE_ZLIB=ON
        -DFT_REQUIRE_PNG=ON
        -DFT_REQUIRE_BZIP2=ON
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(fmt
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DFMT_CUDA_TEST=OFF
        -DFMT_DOC=OFF
        -DFMT_FUZZ=OFF
        -DFMT_INSTALL=ON
        -DFMT_MODULE=OFF
        -DFMT_OS=ON
        -DFMT_PEDANTIC=OFF
        -DFMT_SYSTEM_HEADERS=OFF
        -DFMT_TEST=OFF
        -DFMT_UNICODE=ON
        -DFMT_WERROR=OFF
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(spdlog
    DEPENDS fmt_external
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DSPDLOG_FMT_EXTERNAL=ON
        -DSPDLOG_INSTALL=ON
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_NO_EXCEPTIONS=ON
        -DSPDLOG_BUILD_SHARED=OFF
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(glm
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DGLM_BUILD_TESTS=OFF
        -DGLM_BUILD_LIBRARY=OFF
        -DGLM_ENABLE_CXX_20=ON
        -DGLM_ENABLE_LANG_EXTENSIONS=ON
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(sdl3
    DEPENDS freetype_external
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DSDL_SHARED=OFF
        -DSDL_STATIC=ON
        -DSDL_HIDAPI_LIBUSB=OFF
        -DSDL_RPATH=OFF
        -DSDL_IBUS=OFF
        -DSDL_TEST_LIBRARY=OFF
        -DSDL_TESTS=OFF
        -DSDL_OFFSCREEN=ON
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(lzma
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DBUILD_TESTING=OFF
        -DXZ_TOOL_LZMADEC=OFF
        -DXZ_TOOL_LZMAINFO=OFF
        -DXZ_TOOL_SCRIPTS=OFF
        -DXZ_TOOL_SYMLINKS=OFF
        -DXZ_TOOL_SYMLINKS_LZMA=OFF
        -DXZ_TOOL_XZ=OFF
        -DXZ_TOOL_XZDEC=OFF
        -DXZ_SANDBOX=no
        -DXZ_THREADS=yes
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(libarchive
    DEPENDS lzma_external bzip2_external zlib_external
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DCMAKE_C_FLAGS="${CMAKE_C_FLAGS} /FS"
        -DENABLE_BZ2LIB=ON
        -DENABLE_ZLIB=ON
        -DENABLE_LZMA=ON
        -DENABLE_CAT_SHARED=OFF
        -DENABLE_CAT=OFF
        -DENABLE_CPIO_SHARED=OFF
        -DENABLE_CPIO=OFF
        -DENABLE_EXPAT=OFF
        -DENABLE_ICONV=OFF
        -DENABLE_LIBB2=OFF
        -DENABLE_LZ4=OFF
        -DENABLE_LZO=OFF
        -DENABLE_MBEDTLS=OFF
        -DENABLE_NETTLE=OFF
        -DENABLE_OPENSSL=OFF
        -DENABLE_SAFESEH=OFF
        -DENABLE_TAR_SHARED=OFF
        -DENABLE_TAR=OFF
        -DENABLE_TEST=OFF
        -DENABLE_UNZIP_SHARED=OFF
        -DENABLE_UNZIP=OFF
        -DENABLE_WIN32_XMLLITE=OFF
        -DENABLE_XML2=OFF
        -DENABLE_ZSTD=OFF
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(curl
    DEPENDS zlib_external
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_CURL_EXE=OFF
        -DBUILD_STATIC_CURL=OFF
        -DCURL_USE_LIBPSL=OFF
        -DCURL_USE_SCHANNEL=ON
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(mpg123
    SOURCE_SUBDIR ports/cmake
    PATCH_COMMAND ${CMAKE_COMMAND} -E echo "Patching mpg123 to add VBRI tag support"
        COMMAND ${CMAKE_COMMAND}
            -DFILE_TO_PATCH=<SOURCE_DIR>/src/libmpg123/parse.c
            -P ${CMAKE_SOURCE_DIR}/patch_mpg123.cmake
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DBUILD_LIBOUT123=OFF
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(soundtouch
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DST_NO_EXCEPTION_HANDLING=1
        -DSOUNDSTRETCH=OFF
        -DSOUNDTOUCH_DLL=OFF
        -DOPENMP=OFF
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(soloud
    DEPENDS sdl3_external mpg123_external
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DSOLOUD_WITH_SDL3=ON
        -DSOLOUD_WITH_MPG123=ON
        -DSOLOUD_WITH_FFMPEG=OFF # way too lazy
        -DPKG_CONFIG_ARGN="--msvc-syntax"
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

if(NOT DISABLE_NSYNC)
add_external_project(nsync
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DNSYNC_ENABLE_TESTS=OFF
        -DCMAKE_CXX_STANDARD=17
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)
else()
add_custom_target(nsync_external)
endif()

add_external_project(simdutf
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DSIMDUTF_TESTS=OFF
        -DSIMDUTF_ATOMIC_BASE64_TESTS=OFF
        -DSIMDUTF_BENCHMARKS=OFF
        -DSIMDUTF_TOOLS=OFF
        -DSIMDUTF_ICONV=OFF
        -DSIMDUTF_FUZZERS=OFF
        -DSIMDUTF_COVERAGE=OFF
        -DSIMDUTF_INTERNAL_TESTS=OFF
        -DSIMDUTF_LOGGING=OFF
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

add_external_project(ctre
    CMAKE_ARGS
        ${EXTERNAL_PROJECT_CMAKE_ARGS}
        -DCTRE_BUILD_TESTS=OFF
        -DCTRE_BUILD_PACKAGE=ON
        -DCTRE_BUILD_PACKAGE_DEB=OFF
        -DCTRE_BUILD_PACKAGE_RPM=OFF 
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
)

# Discord SDK (pre-built binary)
add_external_project(discordsdk
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_INSTALL_DIR}/include
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_INSTALL_DIR}/lib/x86
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_INSTALL_DIR}/lib/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/c/discord_game_sdk.h ${DEPS_INSTALL_DIR}/include/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/lib/x86/discord_game_sdk.dll ${DEPS_INSTALL_DIR}/lib/x86/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/lib/x86/discord_game_sdk.dll.lib ${DEPS_INSTALL_DIR}/lib/x86/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/lib/x86_64/discord_game_sdk.dll ${DEPS_INSTALL_DIR}/lib/x86_64/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/lib/x86_64/discord_game_sdk.dll.lib ${DEPS_INSTALL_DIR}/lib/x86_64/
)

# BASS audio libraries (pre-built binaries)
add_external_project(bass_header
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bass/include
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/c/bass.h ${DEPS_CACHE}/bass-${BASS_VERSION}/bass/include/
)

add_external_project(bass
    DEPENDS bass_header_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bass/lib/windows/i686
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bass/lib/windows/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bass.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bass/lib/windows/i686/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/x64/bass.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bass/lib/windows/x86_64/
)

add_external_project(bassfx
    DEPENDS bass_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassfx/include
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassfx/lib/windows/i686
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassfx/lib/windows/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bass_fx.h ${DEPS_CACHE}/bass-${BASS_VERSION}/bassfx/include/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bass_fx.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassfx/lib/windows/i686/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/x64/bass_fx.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassfx/lib/windows/x86_64/
)

add_external_project(bassmix
    DEPENDS bass_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassmix/include
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassmix/lib/windows/i686
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassmix/lib/windows/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bassmix.h ${DEPS_CACHE}/bass-${BASS_VERSION}/bassmix/include/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bassmix.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassmix/lib/windows/i686/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/x64/bassmix.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassmix/lib/windows/x86_64/
)

add_external_project(bassloud
    DEPENDS bass_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassloud/include
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassloud/lib/windows/i686
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassloud/lib/windows/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/c/bassloud.h ${DEPS_CACHE}/bass-${BASS_VERSION}/bassloud/include/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bassloud.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassloud/lib/windows/i686/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/x64/bassloud.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassloud/lib/windows/x86_64/
)

add_external_project(bassasio
    DEPENDS bass_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassasio/include
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassasio/lib/windows/i686
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassasio/lib/windows/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bassasio.h ${DEPS_CACHE}/bass-${BASS_VERSION}/bassasio/include/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bassasio.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassasio/lib/windows/i686/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/x64/bassasio.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassasio/lib/windows/x86_64/
)

add_external_project(basswasapi_header
    DEPENDS bass_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/basswasapi/include
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/c/basswasapi.h ${DEPS_CACHE}/bass-${BASS_VERSION}/basswasapi/include/
)

add_external_project(basswasapi
    DEPENDS basswasapi_header_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/basswasapi/lib/windows/i686
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/basswasapi/lib/windows/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/basswasapi.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/basswasapi/lib/windows/i686/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/x64/basswasapi.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/basswasapi/lib/windows/x86_64/
)

add_external_project(bassflac
    DEPENDS bass_external
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
    BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Skipping build"
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassflac/lib/windows/i686
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEPS_CACHE}/bass-${BASS_VERSION}/bassflac/lib/windows/x86_64
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/bassflac.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassflac/lib/windows/i686/
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/x64/bassflac.dll ${DEPS_CACHE}/bass-${BASS_VERSION}/bassflac/lib/windows/x86_64/
)

# Now build the main project after all dependencies are ready
ExternalProject_Add(neomod_main
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src
    BINARY_DIR ${CMAKE_BINARY_DIR}/neomod
    DEPENDS
        zlib_external
        bzip2_external
        libjpeg_external
        libpng_external
        freetype_external
        fmt_external
        spdlog_external
        glm_external
        sdl3_external
        lzma_external
        libarchive_external
        curl_external
        mpg123_external
        soundtouch_external
        soloud_external
        nsync_external
        simdutf_external
        ctre_external
        discordsdk_external
        bass_header_external
        bass_external
        bassfx_external
        bassmix_external
        bassloud_external
        bassasio_external
        basswasapi_external
        bassflac_external
    CMAKE_ARGS
        -DCMAKE_SYSTEM_NAME=Windows
        -DCMAKE_SYSTEM_PROCESSOR=AMD64
        -DCMAKE_C_COMPILER=cl
        -DCMAKE_CXX_COMPILER=cl
        -DCMAKE_C_OUTPUT_EXTENSION=obj
        -DCMAKE_CXX_OUTPUT_EXTENSION=obj
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
        -DDEPS_INSTALL_DIR=${DEPS_INSTALL_DIR}
        -DDEPS_CACHE=${DEPS_CACHE}
        -DBASS_VERSION=${BASS_VERSION}
        -DWITH_LIVEPP=${WITH_LIVEPP}
        -DCMAKE_PREFIX_PATH=${DEPS_INSTALL_DIR}
        -DCMAKE_FIND_ROOT_PATH=${DEPS_INSTALL_DIR}
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
        -DCMAKE_C_FLAGS=${COMMON_C_FLAGS_STR}
        -DCMAKE_CXX_FLAGS=${COMMON_CXX_FLAGS_STR}
        -DCMAKE_C_FLAGS_DEBUG=${DEBUG_FLAGS_STR}
        -DCMAKE_CXX_FLAGS_DEBUG=${DEBUG_FLAGS_STR}
        -DCMAKE_C_FLAGS_RELEASE=${RELEASE_FLAGS_STR}
        -DCMAKE_CXX_FLAGS_RELEASE=${RELEASE_FLAGS_STR}
        -DCMAKE_EXE_LINKER_FLAGS_DEBUG=${DEBUG_LINK_FLAGS_STR}
        -DCMAKE_EXE_LINKER_FLAGS_RELEASE=${RELEASE_LINK_FLAGS_STR}
        -DCMAKE_COLOR_DIAGNOSTICS=ON
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    BUILD_COMMAND cmake --build <BINARY_DIR> --config ${CMAKE_BUILD_TYPE} --parallel ${CPU_COUNT}
    # INSTALL_COMMAND cmake --install <BINARY_DIR> --config ${CMAKE_BUILD_TYPE}
)

# Build summary
message(STATUS "neomod Superbuild Configuration:")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Parallel Jobs: ${CPU_COUNT}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Dependencies Cache: ${DEPS_CACHE}")
message(STATUS "  Dependencies Install: ${DEPS_INSTALL_DIR}")
