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

set(SDL3_VERSION "bd9f6ca9ff0e9509f49980fc1b70837f54b6f244")
set(SDL3_URL "https://github.com/libsdl-org/SDL/archive/${SDL3_VERSION}.tar.gz")
set(SDL3_HASH "SHA512=96ab2ca27d7be58d843f1e7c55f1c60413a9d49a82cc15f3cd5f92825b5de680c2f1f16a509f51c8124d6e5f3218b43f39000e342694014c349ae4131aa03b66")
set_download_name("sdl3" "${SDL3_VERSION}" "${SDL3_URL}")

set(BROTLI_VERSION "1.2.1")
set(BROTLI_URL "https://github.com/google/brotli/archive/refs/tags/v${BROTLI_VERSION}.tar.gz")
set(BROTLI_HASH "SHA512=460591287d428c8b5a8a2766b5b424d14501fcda12c7118b8f3cc4bfae44b90ac75ca156557f07b6e22542612724d904533e5f70019be07dbbb604c46f03ab69")
set_download_name("brotli" "${BROTLI_VERSION}" "${BROTLI_URL}")

set(FREETYPE_VERSION "2.14.3")
string(REPLACE "." "-" _freetype_ver_temp "${FREETYPE_VERSION}")
set(FREETYPE_URL "https://github.com/freetype/freetype/archive/refs/tags/VER-${_freetype_ver_temp}.tar.gz")
set(FREETYPE_HASH "SHA512=c3b6b0cc4b428c9c647ab2148386901dfd315273b68051940e8fea6010d46fdd2913467c3ef58be0d499b8e2ef5a0f1a4cc5e739756155587f4f7dff08ef9695")
set_download_name("freetype" "${FREETYPE_VERSION}" "${FREETYPE_URL}")
unset(_freetype_ver_temp)

set(LIBJPEG_VERSION "3.2.1")
set(LIBJPEG_URL "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${LIBJPEG_VERSION}/libjpeg-turbo-${LIBJPEG_VERSION}.tar.gz")
set(LIBJPEG_HASH "SHA512=8a0be9e42f4dd0634d61ec76b959f8942c70237848bb2854baad8abd352483784b7c4f099823368fb3e40be5d9200fb4b2574aecdcebf225d74b09dc217948eb")
set_download_name("libjpeg" "${LIBJPEG_VERSION}" "${LIBJPEG_URL}")

set(LIBPNG_VERSION "1.6.59")
set(LIBPNG_URL "https://github.com/pnggroup/libpng/archive/refs/tags/v${LIBPNG_VERSION}.tar.gz")
set(LIBPNG_HASH "SHA512=2d753e764846a0a75a10f2a00c6f4ae23d2ae37237c69d59d5e619da3f143d59f4ea9ce29c1fb39435df1cef77877c83b042a2275934c353a6603061da2d2dd9")
set_download_name("libpng" "${LIBPNG_VERSION}" "${LIBPNG_URL}")

set(ZLIB_VERSION "2.3.3")
set(ZLIB_URL "https://github.com/zlib-ng/zlib-ng/archive/refs/tags/${ZLIB_VERSION}.tar.gz")
set(ZLIB_HASH "SHA512=e2057c764f1d5aaee738edee7e977182c5b097e3c95489dcd8de813f237d92a05daaa86d68d44b331d9fec5d1802586a8f6cfb658ba849874aaa14e72a8107f5")
set_download_name("zlib" "${ZLIB_VERSION}" "${ZLIB_URL}")

set(BZIP2_VERSION "1ea1ac188ad4b9cb662e3f8314673c63df95a589")
set(BZIP2_URL "https://github.com/libarchive/bzip2/archive/${BZIP2_VERSION}.tar.gz")
set(BZIP2_HASH "SHA512=a1aae1e884f85a225e2a1ddf610f11dda672bc242d4e8d0cda3534efb438b3a0306ec1d130eec378d46abb48f6875687d6b20dcc18a6037a4455f531c22d50f6")
set_download_name("bzip2" "${BZIP2_VERSION}" "${BZIP2_URL}")

set(FMT_VERSION "0fc31633e7d5")
set(FMT_URL "https://github.com/fmtlib/fmt/archive/${FMT_VERSION}.tar.gz")
set(FMT_HASH "SHA512=a47ddb64a017fefcb291591ecea61842abc64d663575fba7e0cfc198cd843395a174fde5823fc89e05c21ff2af8b94caacc1b82139b5d19ffa36a8c58f4821de")
set_download_name("fmt" "${FMT_VERSION}" "${FMT_URL}")

set(SPDLOG_VERSION "1.17.0")
set(SPDLOG_URL "https://github.com/gabime/spdlog/archive/refs/tags/v${SPDLOG_VERSION}.tar.gz")
set(SPDLOG_HASH "SHA512=8df117055d19ff21c9c9951881c7bdf27cc0866ea3a4aa0614b2c3939cedceab94ac9abaa63dc4312b51562b27d708cb2f014c68c603fd1c1051d3ed5c1c3087")
set_download_name("spdlog" "${SPDLOG_VERSION}" "${SPDLOG_URL}")

set(GLM_VERSION "1.0.3")
set(GLM_URL "https://github.com/g-truc/glm/archive/refs/tags/${GLM_VERSION}.tar.gz")
set(GLM_HASH "SHA512=0a490f0c79cd4a8ba54f37358f8917cef961dab9e61417c84ae0959c61bc860e5b83f4fb7f27169fb3d08eef1d84131bddde23d60876922310205c901b1273aa")
set_download_name("glm" "${GLM_VERSION}" "${GLM_URL}")

set(LZMA_VERSION "5.8.4")
set(LZMA_URL "https://github.com/tukaani-project/xz/releases/download/v${LZMA_VERSION}/xz-${LZMA_VERSION}.tar.gz")
set(LZMA_HASH "SHA512=94db303688511a2d34f81ba994d834b28b0145a1ecd75ec2c61416b26c9cc24ab3e4d902808cc3d785c5894793d57af569728be7bc8ef6624bb3fc90c04f97fa")
set_download_name("lzma" "${LZMA_VERSION}" "${LZMA_URL}")

set(LIBARCHIVE_VERSION "3.8.10")
set(LIBARCHIVE_URL "https://github.com/libarchive/libarchive/releases/download/v${LIBARCHIVE_VERSION}/libarchive-${LIBARCHIVE_VERSION}.tar.gz")
set(LIBARCHIVE_HASH "SHA512=823e5424ed675d73cadb05e73e168110f5748991dd1f3b3767822f6b3795ecc8bd92c6f34cce95712faebc7b1bbd20fc8aff80b991e4803ef7a055c49358bf65")
set_download_name("libarchive" "${LIBARCHIVE_VERSION}" "${LIBARCHIVE_URL}")

set(MPG123_VERSION "9bc3de631d646026c69a84fd0e3def259cca4841")
set(MPG123_URL "https://github.com/madebr/mpg123/archive/${MPG123_VERSION}.tar.gz")
set(MPG123_HASH "SHA512=2f35ddd23c5f96f781a8b94a5a3757243d9fcd55bfb759d656fdc0794623f1ad19764063a411163f4ccf9b11ca6a9890ae8f717ae9df44146dc4f824885c6eef")
set_download_name("mpg123" "${MPG123_VERSION}" "${MPG123_URL}")

set(SOLOUD_VERSION "4d0ab5baa88e06d2953caa13ec28aaf4c0f8819b")
set(SOLOUD_URL "https://github.com/neomodnet/neoloud/archive/${SOLOUD_VERSION}.tar.gz")
set(SOLOUD_HASH "SHA512=e1dfd833564e7b72377751d5507ed695f28bb449dadee914687f42a60a955279b8ad5bf112654b364de2455935068a1be25ecaa5e52dd5bac5b2803571828f2b")
set_download_name("soloud" "${SOLOUD_VERSION}" "${SOLOUD_URL}")

set(NSYNC_VERSION "1.30.0")
set(NSYNC_URL "https://github.com/google/nsync/archive/refs/tags/${NSYNC_VERSION}.tar.gz")
set(NSYNC_HASH "SHA512=fdcd61eb686ca6d6804d82837fcd33ddee54d6b2aeb7bc20cdff8c5bd2a75f87b724f72c7e835459a1a82ee8bed3d6da5e4c111b3bca22545c6e037f129839f2")
set_download_name("nsync" "${NSYNC_VERSION}" "${NSYNC_URL}")

set(SIMDUTF_VERSION "9.3.2")
set(SIMDUTF_URL "https://github.com/simdutf/simdutf/archive/refs/tags/v${SIMDUTF_VERSION}.tar.gz")
set(SIMDUTF_HASH "SHA512=cba5850d7182305da9f4e4f6029c3f1b46a1b407d54ae2661c805e734490cb435ad2a641e44bcfcf05726f5d927b0ac37d510b858e61c352118e0e63c323d980")
set_download_name("simdutf" "${SIMDUTF_VERSION}" "${SIMDUTF_URL}")

set(CTRE_VERSION "62b060684d57")
set(CTRE_URL "https://github.com/hanickadot/compile-time-regular-expressions/archive/${CTRE_VERSION}.tar.gz")
set(CTRE_HASH "SHA512=bd0c0b872d795f179680280d44744e4a8091067c1f340763707fec8adb88994641ea7e099c7dc3149f1b2d3848d2507c5b5b015f8c45ea9992519a64dccf1cb6")
set_download_name("ctre" "${CTRE_VERSION}" "${CTRE_URL}")

set(CURL_VERSION "8.23.0")
string(REPLACE "." "_" _curl_ver_temp "${CURL_VERSION}")
set(CURL_URL "https://github.com/curl/curl/releases/download/curl-${_curl_ver_temp}/curl-${CURL_VERSION}.tar.gz")
set(CURL_HASH "SHA512=e05a7728b24b6d3dffab3e8dd50031e36412132945c5997413f7dae2b2fa3854f07c0f2f28d8251536776f8f2df42ed221d32cce07bb72d28f24fafb8110166b")
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
