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

set(SDL3_VERSION "a45ef5dc6443e2ebcfc085b10b4ea4a3d145ea63")
set(SDL3_URL "https://github.com/libsdl-org/SDL/archive/${SDL3_VERSION}.tar.gz")
set(SDL3_HASH "SHA512=a621938c323cc8100a354e185ebc389bfc04a256979482f7ecd8eb527411249b7909060c1a32dc8952bad0492c1f199aefead1e4dd14e8d36c11b6cfe0217b18")
set_download_name("sdl3" "${SDL3_VERSION}" "${SDL3_URL}")

set(BROTLI_VERSION "1.2.0")
set(BROTLI_URL "https://github.com/google/brotli/archive/refs/tags/v${BROTLI_VERSION}.tar.gz")
set(BROTLI_HASH "SHA512=f94542afd2ecd96cc41fd21a805a3da314281ae558c10650f3e6d9ca732b8425bba8fde312823f0a564c7de3993bdaab5b43378edab65ebb798cefb6fd702256")
set_download_name("brotli" "${BROTLI_VERSION}" "${BROTLI_URL}")

set(FREETYPE_VERSION "2.14.3")
string(REPLACE "." "-" _freetype_ver_temp "${FREETYPE_VERSION}")
set(FREETYPE_URL "https://github.com/freetype/freetype/archive/refs/tags/VER-${_freetype_ver_temp}.tar.gz")
set(FREETYPE_HASH "SHA512=c3b6b0cc4b428c9c647ab2148386901dfd315273b68051940e8fea6010d46fdd2913467c3ef58be0d499b8e2ef5a0f1a4cc5e739756155587f4f7dff08ef9695")
set_download_name("freetype" "${FREETYPE_VERSION}" "${FREETYPE_URL}")
unset(_freetype_ver_temp)

set(LIBJPEG_VERSION "3.1.4.1")
set(LIBJPEG_URL "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${LIBJPEG_VERSION}/libjpeg-turbo-${LIBJPEG_VERSION}.tar.gz")
set(LIBJPEG_HASH "SHA512=d82c2c2bd8abb1b88a245cece407a4cf65c378003e105a99a20ae4e7e3a7282b64874c3e7d8c003e83b43c990d43f860066e6ac57c143f8b3b9732d6bca7d94a")
set_download_name("libjpeg" "${LIBJPEG_VERSION}" "${LIBJPEG_URL}")

set(LIBPNG_VERSION "1.6.58")
set(LIBPNG_URL "https://github.com/pnggroup/libpng/archive/refs/tags/v${LIBPNG_VERSION}.tar.gz")
set(LIBPNG_HASH "SHA512=65f54d805e1f7c46a5fc335b984e4cbd4f934e0f02fbf6673c13800b49a4c11fbeb4098eebfb33079527a56c3d933e97631f91ab68dbb31442982784f9241ace")
set_download_name("libpng" "${LIBPNG_VERSION}" "${LIBPNG_URL}")

set(ZLIB_VERSION "2.3.3")
set(ZLIB_URL "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${ZLIB_VERSION}.tar.gz")
set(ZLIB_HASH "SHA512=e2057c764f1d5aaee738edee7e977182c5b097e3c95489dcd8de813f237d92a05daaa86d68d44b331d9fec5d1802586a8f6cfb658ba849874aaa14e72a8107f5")
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

set(LZMA_VERSION "5.8.3")
set(LZMA_URL "https://github.com/tukaani-project/xz/releases/download/v${LZMA_VERSION}/xz-${LZMA_VERSION}.tar.gz")
set(LZMA_HASH "SHA512=bd77164795b5cbfbe864f64021e67e37f39cb9aba9abdd894d53fbb6857abe074923808918d1dc3bb0706253e726b2b9704cd0c3bc744d70e220c7356fa4995e")
set_download_name("lzma" "${LZMA_VERSION}" "${LZMA_URL}")

set(LIBARCHIVE_VERSION "3.8.7")
set(LIBARCHIVE_URL "https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.gz")
set(LIBARCHIVE_HASH "SHA512=a13c9342aba25f50efc08f6753631f46e30c5806c61987fe2640c8d58d18c7e90ff0da5b8a13a0589e9978ea08253ac2edb533ac254b70ea391f94b5f1eede42")
set_download_name("libarchive" "${LIBARCHIVE_VERSION}" "${LIBARCHIVE_URL}")

set(MPG123_VERSION "4080a897fca4f6923641588dce91da613193c888")
set(MPG123_URL "https://github.com/madebr/mpg123/archive/${MPG123_VERSION}.tar.gz")
set(MPG123_HASH "SHA512=726ca44b87ae689a27f830104b5d452bdc7d63a0de6cd88441900ec103c8b00d36bee9df8afb954f40ae27e75bcbdfe31c2614dc933a20e8679f5640e1856b2e")
set_download_name("mpg123" "${MPG123_VERSION}" "${MPG123_URL}")

set(SOUNDTOUCH_VERSION "2.4.0")
set(SOUNDTOUCH_URL "https://codeberg.org/soundtouch/soundtouch/archive/${SOUNDTOUCH_VERSION}.tar.gz")
set(SOUNDTOUCH_HASH "SHA512=8bd199c6363104ba6c9af1abbd3c4da3567ccda5fe3a68298917817fc9312ecb0914609afba1abd864307b0a596becf450bc7073eeec17b1de5a7c5086fbc45e")
set_download_name("soundtouch" "${SOUNDTOUCH_VERSION}" "${SOUNDTOUCH_URL}")

set(SOLOUD_VERSION "7381a0eeb8c357346daeacc98af55afc16db4f96")
set(SOLOUD_URL "https://github.com/neomodnet/neoloud/archive/${SOLOUD_VERSION}.tar.gz")
set(SOLOUD_HASH "SHA512=559cf2773732be005fcdb18e67812ce29556431f4e91f7ce15c92214f4e77e3b3647d5233e7cdad9bf76f6786d15241317c1020f0191238b7dc8976c0514206c")
set_download_name("soloud" "${SOLOUD_VERSION}" "${SOLOUD_URL}")

set(NSYNC_VERSION "1.30.0")
set(NSYNC_URL "https://github.com/google/nsync/archive/refs/tags/${NSYNC_VERSION}.tar.gz")
set(NSYNC_HASH "SHA512=fdcd61eb686ca6d6804d82837fcd33ddee54d6b2aeb7bc20cdff8c5bd2a75f87b724f72c7e835459a1a82ee8bed3d6da5e4c111b3bca22545c6e037f129839f2")
set_download_name("nsync" "${NSYNC_VERSION}" "${NSYNC_URL}")

set(SIMDUTF_VERSION "9.0.0")
set(SIMDUTF_URL "https://github.com/simdutf/simdutf/archive/refs/tags/v${SIMDUTF_VERSION}.tar.gz")
set(SIMDUTF_HASH "SHA512=0c74226247cbe95368efa87ab84f5217485f16bcdf7a9def8741c6086cb86e6c378f0c437030d2be0934726e3ea9c28b5df2e593d0c654c78291c455a8d1e103")
set_download_name("simdutf" "${SIMDUTF_VERSION}" "${SIMDUTF_URL}")

set(CTRE_VERSION "6225211806c48230e5d17a1e555ef69e7325051c")
set(CTRE_URL "https://github.com/hanickadot/compile-time-regular-expressions/archive/${CTRE_VERSION}.tar.gz")
set(CTRE_HASH "SHA512=7633ac6297e61e1f2f59468b0eeb29d8f010dc069b0e866d261744cfa0a772c7ac2ae4818bb563cd578db5a1d86bc37f12aa9cddc87d551cba2942239ab09c87")
set_download_name("ctre" "${CTRE_VERSION}" "${CTRE_URL}")

set(CURL_VERSION "8.19.0")
string(REPLACE "." "_" _curl_ver_temp "${CURL_VERSION}")
set(CURL_URL "https://github.com/curl/curl/releases/download/curl-${_curl_ver_temp}/curl-${CURL_VERSION}.tar.gz")
set(CURL_HASH "SHA512=745572f0cb9096ff88f737392d1ac25052fc8cff6c35bd09f970301e5e211e3b113f6c184ab2a5ae8c64ab989a9b1fdd6cbcb5d85a0b01d525706124c3ec1e4b")
set_download_name("curl" "${CURL_VERSION}" "${CURL_URL}")
unset(_curl_ver_temp)

set(DISCORDRPC_COMMIT "deedb52ce511728458641783cb6d80c7bbdde38c")
set(DISCORDRPC_URL "https://github.com/harmonytf/discord-rpc/archive/${DISCORDRPC_COMMIT}.tar.gz")
set(DISCORDRPC_HASH "SHA512=35e807a8901458cdaaac89e0464785ec326df219268bc8bbd08876ff40bc9f0d01b6edfabfe3e333dc367c04f6fa74d1004ca301bae95582308cbd1a0aa8eb5e")
set_download_name("discordrpc" "${DISCORDRPC_COMMIT}" "${DISCORDRPC_URL}")

# discord-rpc bundles rapidjson via submodule, but we pre-stage it ourselves to avoid the cmake-time download
set(RAPIDJSON_COMMIT "8261c1ddf43f10de00fd8c9a67811d1486b2c784")
set(RAPIDJSON_URL "https://github.com/Tencent/rapidjson/archive/${RAPIDJSON_COMMIT}.tar.gz")
set(RAPIDJSON_HASH "SHA512=462dd6fad8eed048cc1fef57337742f483ac7df7742cb3ab772cc943cc0a8ff279c38d3078af36b0283c40b75cf7667a166d7af6af2fb93eb0df8127ca879025")
set_download_name("rapidjson" "${RAPIDJSON_COMMIT}" "${RAPIDJSON_URL}")

# BINARY DEPENDENCIES

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
