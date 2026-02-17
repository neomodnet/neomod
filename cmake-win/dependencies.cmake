# Dependencies configuration for neomod
# This file contains version information, URLs, and hashes for all external dependencies

# helper wrapper around ExternalProject_Add to avoid repetition in the top-level CMakeLists
function(add_external_project dep_name)
    string(TOUPPER "${dep_name}" _upper_dep_name)
    ExternalProject_Add(${dep_name}_external
        URL ${${_upper_dep_name}_URL}
        URL_HASH ${${_upper_dep_name}_HASH}
        DOWNLOAD_NAME ${${_upper_dep_name}_DL_NAME}
        DOWNLOAD_DIR ${DEPS_CACHE}
        ${ARGN}
    )
endfunction()

# helper macro to set download name based on URL extension
macro(set_download_name dep_name version url)
    string(TOUPPER "${dep_name}" _upper_dep_name)
    string(REGEX MATCH "[^/]+$" _temp_filename "${url}")
    string(REGEX MATCH "\\.[^.]+(\\.gz)?$" _temp_ext "${_temp_filename}")
    set(${_upper_dep_name}_DL_NAME "${dep_name}-${version}${_temp_ext}")
endmacro()

set(SDL3_VERSION "e049fff3605b40ff3c3956785b3812fc645b2a7c")
set(SDL3_URL "https://github.com/libsdl-org/SDL/archive/${SDL3_VERSION}.tar.gz")
set(SDL3_HASH "SHA512=9de76287350913fc563fbcb4a69c68e717a343e492e233f04895cf59455fd689e2f4148d6c022b7112323b5be8d027169008b1e8e32a5275a2c93a375b1721b6")
set_download_name("sdl3" "${SDL3_VERSION}" "${SDL3_URL}")

set(FREETYPE_VERSION "2.14.1")
string(REPLACE "." "-" _freetype_ver_temp "${FREETYPE_VERSION}")
set(FREETYPE_URL "https://github.com/freetype/freetype/archive/refs/tags/VER-${_freetype_ver_temp}.tar.gz")
set(FREETYPE_HASH "SHA512=b73b08784bb4b293fb807e4ca5585fa490da11c3b0f9dca26e39e4cdaf4551ce7d75006e97ca721bfca2ed53f4c6f94c12e3d6c606955ec7c0dcd2f48bfa613d")
set_download_name("freetype" "${FREETYPE_VERSION}" "${FREETYPE_URL}")
unset(_freetype_ver_temp)

set(LIBJPEG_VERSION "3.1.3")
set(LIBJPEG_URL "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${LIBJPEG_VERSION}/libjpeg-turbo-${LIBJPEG_VERSION}.tar.gz")
set(LIBJPEG_HASH "SHA512=d3410a072044b4962c1aa08eb144b4e4b959f4f65203dfac4013b14e2fd987b9a6ee9b59f5570980fa691ddf5e9f9d3aa328a63afb487a46c2e76de722f3d693")
set_download_name("libjpeg" "${LIBJPEG_VERSION}" "${LIBJPEG_URL}")

set(LIBPNG_VERSION "1.6.54")
set(LIBPNG_URL "https://github.com/pnggroup/libpng/archive/refs/tags/v${LIBPNG_VERSION}.tar.gz")
set(LIBPNG_HASH "SHA512=f09e41c5e05714760b8dd05bd11bfe8b6b5840c277e5377a3854f5260879b69709c708f9581c07bdc2ffafda45796066ec258b67b0e96af64614b12346bedb1b")
set_download_name("libpng" "${LIBPNG_VERSION}" "${LIBPNG_URL}")

set(ZLIB_VERSION "2.3.2")
set(ZLIB_URL "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${ZLIB_VERSION}.tar.gz")
set(ZLIB_HASH "SHA512=8781ee4bfda7cb8c8c5150c2e6a067d699580616b61af2ea4cf03cbe14c6715b31a29a20b7c3dd97254a9e487c72c5228c9cfa817ff71aa765fe7043ab136f04")
set_download_name("zlib" "${ZLIB_VERSION}" "${ZLIB_URL}")

set(BZIP2_VERSION "1ea1ac188ad4b9cb662e3f8314673c63df95a589")
set(BZIP2_URL "https://github.com/libarchive/bzip2/archive/${BZIP2_VERSION}.tar.gz")
set(BZIP2_HASH "SHA512=a1aae1e884f85a225e2a1ddf610f11dda672bc242d4e8d0cda3534efb438b3a0306ec1d130eec378d46abb48f6875687d6b20dcc18a6037a4455f531c22d50f6")
set_download_name("bzip2" "${BZIP2_VERSION}" "${BZIP2_URL}")

set(FMT_VERSION "0e078f6ed0624be8babc43bd145371d9f3a08aab")
set(FMT_URL "https://github.com/fmtlib/fmt/archive/${FMT_VERSION}.tar.gz")
set(FMT_HASH "SHA512=df87cbd340ddb6ea272fd8f7eff7f8bb7d9d0e75cb5d1fbda4cdb7f05222478c47a1e8bc72fcb58bb6810d370a71c967232f72b3db3644eba3f56f770a44abbc")
set_download_name("fmt" "${FMT_VERSION}" "${FMT_URL}")

set(SPDLOG_VERSION "1.17.0")
set(SPDLOG_URL "https://github.com/gabime/spdlog/archive/refs/tags/v${SPDLOG_VERSION}.tar.gz")
set(SPDLOG_HASH "SHA512=8df117055d19ff21c9c9951881c7bdf27cc0866ea3a4aa0614b2c3939cedceab94ac9abaa63dc4312b51562b27d708cb2f014c68c603fd1c1051d3ed5c1c3087")
set_download_name("spdlog" "${SPDLOG_VERSION}" "${SPDLOG_URL}")

set(GLM_VERSION "1.0.3")
set(GLM_URL "https://github.com/g-truc/glm/archive/refs/tags/${GLM_VERSION}.tar.gz")
set(GLM_HASH "SHA512=0a490f0c79cd4a8ba54f37358f8917cef961dab9e61417c84ae0959c61bc860e5b83f4fb7f27169fb3d08eef1d84131bddde23d60876922310205c901b1273aa")
set_download_name("glm" "${GLM_VERSION}" "${GLM_URL}")

set(LZMA_VERSION "5.8.2")
set(LZMA_URL "https://github.com/tukaani-project/xz/releases/download/v${LZMA_VERSION}/xz-${LZMA_VERSION}.tar.gz")
set(LZMA_HASH "SHA512=0b808fc8407e7c50da3a7b2db05be732c2fcd41850b92c7f5647181443483848ff359e176c816ce2038c115273f51575877c14f1356417cc9d53845841acb063")
set_download_name("lzma" "${LZMA_VERSION}" "${LZMA_URL}")

set(LIBARCHIVE_VERSION "3.8.5")
set(LIBARCHIVE_URL "https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.gz")
set(LIBARCHIVE_HASH "SHA512=81b11d433636fd19967c74e84498529d14722ed56164637380f449a1e096c20684eb9d7f3fec99ce7a7912bc5a20c06b892afd4d4167cb35476ee2c421943e4d")
set_download_name("libarchive" "${LIBARCHIVE_VERSION}" "${LIBARCHIVE_URL}")

set(MPG123_VERSION "a06133928e6518bd65314c9cea12ccb5588703e9")
set(MPG123_URL "https://github.com/madebr/mpg123/archive/${MPG123_VERSION}.tar.gz")
set(MPG123_HASH "SHA512=a1c2767c628432c4adcdbc04e2e6441ff6af3033dc5fae927b845f90eb1fb9f407e0a7e196702a51aa768d5348432abc6604746ff7a5dffb1f1d72241b7da30a")
set_download_name("mpg123" "${MPG123_VERSION}" "${MPG123_URL}")

set(SOUNDTOUCH_VERSION "2.4.0")
set(SOUNDTOUCH_URL "https://codeberg.org/soundtouch/soundtouch/archive/${SOUNDTOUCH_VERSION}.tar.gz")
set(SOUNDTOUCH_HASH "SHA512=8bd199c6363104ba6c9af1abbd3c4da3567ccda5fe3a68298917817fc9312ecb0914609afba1abd864307b0a596becf450bc7073eeec17b1de5a7c5086fbc45e")
set_download_name("soundtouch" "${SOUNDTOUCH_VERSION}" "${SOUNDTOUCH_URL}")

set(SOLOUD_VERSION "6ce4e8d0d8c4a281c6aa380b89bcbaa73ea01c39")
set(SOLOUD_URL "https://github.com/whrvt/neoloud/archive/${SOLOUD_VERSION}.tar.gz")
set(SOLOUD_HASH "SHA512=c3f2c6c6a5e601422a77b9635848cebef008d67935094825502751a87ba12f7eb58efd425d6ad83a848937b0fae5c573f2e6088d94cb7b26f721329353381524")
set_download_name("soloud" "${SOLOUD_VERSION}" "${SOLOUD_URL}")

set(NSYNC_VERSION "1.30.0")
set(NSYNC_URL "https://github.com/google/nsync/archive/refs/tags/${NSYNC_VERSION}.tar.gz")
set(NSYNC_HASH "SHA512=fdcd61eb686ca6d6804d82837fcd33ddee54d6b2aeb7bc20cdff8c5bd2a75f87b724f72c7e835459a1a82ee8bed3d6da5e4c111b3bca22545c6e037f129839f2")
set_download_name("nsync" "${NSYNC_VERSION}" "${NSYNC_URL}")

set(SIMDUTF_VERSION "8.0.0")
set(SIMDUTF_URL "https://github.com/simdutf/simdutf/archive/refs/tags/v${SIMDUTF_VERSION}.tar.gz")
set(SIMDUTF_HASH "SHA512=ee30ac7b7b96dfef2ced3938b1cc8e10cd5ec5b3d35ac9679f30fbb7811bf3f930a31f5b4cf0b4002f27eba84d2e27e7c7bd910d96aa04485588188ad910361d")
set_download_name("simdutf" "${SIMDUTF_VERSION}" "${SIMDUTF_URL}")

set(CTRE_VERSION "6225211806c48230e5d17a1e555ef69e7325051c")
set(CTRE_URL "https://github.com/hanickadot/compile-time-regular-expressions/archive/${CTRE_VERSION}.tar.gz")
set(CTRE_HASH "SHA512=7633ac6297e61e1f2f59468b0eeb29d8f010dc069b0e866d261744cfa0a772c7ac2ae4818bb563cd578db5a1d86bc37f12aa9cddc87d551cba2942239ab09c87")
set_download_name("ctre" "${CTRE_VERSION}" "${CTRE_URL}")

set(CURL_VERSION "8.18.0")
string(REPLACE "." "_" _curl_ver_temp "${CURL_VERSION}")
set(CURL_URL "https://github.com/curl/curl/releases/download/curl-${_curl_ver_temp}/curl-${CURL_VERSION}.tar.gz")
set(CURL_HASH "SHA512=84f193f28369ccb7fba0d8933cfc24f5fbb282b046e7e8c2c1a0da35db8ec13d17e6407c240ce3a12cf4dccac62e5919bd98f3add77065408c6259cfe1071575")
set_download_name("curl" "${CURL_VERSION}" "${CURL_URL}")
unset(_curl_ver_temp)

# BINARY DEPENDENCIES

set(DISCORDSDK_VERSION "2.5.6")
set(DISCORDSDK_URL "https://web.archive.org/web/20250505113314/https://dl-game-sdk.discordapp.net/${DISCORDSDK_VERSION}/discord_game_sdk.zip")
set(DISCORDSDK_HASH "SHA512=4c8f72c7bdf92bc969fb86b96ea0d835e01b9bab1a2cc27ae00bdac1b9733a1303ceadfe138c24a7609b76d61d49999a335dd596cf3f335d894702e2aa23406f")
set_download_name("discordsdk" "${DISCORDSDK_VERSION}" "${DISCORDSDK_URL}")

# BASS BINARIES
set(BASS_VERSION "20260208_202602")

set(BASS_HEADER_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/bass24-header.zip")
set(BASS_HEADER_HASH "SHA512=d7725d6a360c12ec8f7d814b144241750e084f9a6ed4fa9abc9cebb845301dbb0591d9b74b807ccafe94cd1e9bbba5d118b1db757ee9da580b5869b1a6a4f746")
set_download_name("bass_header" "${BASS_VERSION}" "${BASS_HEADER}")

set(BASS_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/bass.zip")
set(BASS_HASH "SHA512=07282d1db1d7bb9f0a4901c1797a257882863e5f1526e3b053a60ced3c4056fa1b02f1fb856e8ee239286fbe1576110141c50eeff2254b1c69016f0029f73c97")
set_download_name("bass" "${BASS_VERSION}" "${BASS_URL}")

set(BASSFX_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/bass_fx.zip")
set(BASSFX_HASH "SHA512=2fbed47a031d8e49e99c335d50e46f3620132d63fc56f82a71e59e694567a3833a3704345bd822813e01dbca0595ee33351ebcb52a4c44d90e4a673bc0f5f98e")
set_download_name("bassfx" "${BASS_VERSION}" "${BASSFX_URL}")

set(BASSMIX_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/bassmix.zip")
set(BASSMIX_HASH "SHA512=583e3c1f93736034892f37820c7121521eb441f713ac696524491f76acba3b83f7f602f90f2c7563dafc7e122b35dcf639cb62eaab353f81e1dd0e46bd4992c3")
set_download_name("bassmix" "${BASS_VERSION}" "${BASSMIX_URL}")

set(BASSWASAPI_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/basswasapi.zip")
set(BASSWASAPI_HASH "SHA512=f5c68062936ccf60383c5dbea3dc4b9bcf52884fb745d0564bd6592e88a7336e16e5d9a63ea28177385641790d53eaa5d9edfe112e768dcfb68b82af65affddc")
set_download_name("basswasapi" "${BASS_VERSION}" "${BASSWASAPI_URL}")

set(BASSWASAPI_HEADER_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/basswasapi24-header.zip")
set(BASSWASAPI_HEADER_HASH "SHA512=8eee38b038330503ec0b6380c6baa33554d8cdc4263b3fe14ecebf9aff5f175c7fed26e641bb4df87998f630c854ed48bd0703275e38b09f0094628211c36b2e")
set_download_name("basswasapi_header" "${BASS_VERSION}" "${BASSWASAPI_HEADER_URL}")

set(BASSASIO_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/bassasio.zip")
set(BASSASIO_HASH "SHA512=d112ce98255df6b80ee1d34fe7261673d01847d494d7064db5cc99ac97bfc43a9369cab1617d7fdeeb26a0f83c7049946491de9a41b6ad150defc603e74d167a")
set_download_name("bassasio" "${BASS_VERSION}" "${BASSASIO_URL}")

set(BASSLOUD_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/bassloud24.zip")
set(BASSLOUD_HASH "SHA512=cb6abbd23f6766dbceced372aeb733bd663be06efcd73a9040751c90e9bffcbdc8b6774991a41edfb91722cc28aa782708fcaa9378b65cce53e3ba5c8e71c0c0")
set_download_name("bassloud" "${BASS_VERSION}" "${BASSLOUD_URL}")

set(BASSFLAC_URL "https://archive.org/download/bass-libs-${BASS_VERSION}/bassflac24.zip")
set(BASSFLAC_HASH "SHA512=92df226490926979761cf838140a4a3279afe3b77ac2c0b00536b56f7ba7c328b31770788c5daf1449a3d6e8d7759c5df18465e7150f3c3fdf35e83e765a144d")
set_download_name("bassflac" "${BASS_VERSION}" "${BASSFLAC_URL}")
