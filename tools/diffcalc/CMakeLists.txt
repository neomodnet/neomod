# Standalone osu! difficulty/ppv2 calculator, built from a subset of the neomod sources.
# Requires: C++23 compiler, GLM headers (downloaded automatically if not installed)

cmake_minimum_required(VERSION 3.24)

project(diffcalc LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "build type" FORCE)
endif()

cmake_path(SET NEOMOD_SRC NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

# GLM: prefer an installed copy (vcpkg, homebrew, distro), otherwise fetch it
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    include(FetchContent)
    set(GLM_BUILD_TESTS OFF)
    set(GLM_BUILD_LIBRARY OFF)
    set(GLM_ENABLE_CXX_20 ON)
    set(GLM_ENABLE_LANG_EXTENSIONS ON)
    FetchContent_Declare(glm
        URL "https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz"
        URL_HASH "SHA512=0a490f0c79cd4a8ba54f37358f8917cef961dab9e61417c84ae0959c61bc860e5b83f4fb7f27169fb3d08eef1d84131bddde23d60876922310205c901b1273aa"
    )
    FetchContent_MakeAvailable(glm)
endif()

if(TARGET glm::glm-header-only)
    set(GLM_TARGET glm::glm-header-only)
else()
    set(GLM_TARGET glm::glm)
endif()

add_executable(diffcalc
    "${NEOMOD_SRC}/App/Neomod/DatabaseBeatmap.cpp"
    "${NEOMOD_SRC}/App/Neomod/DiffCalc/DifficultyCalculator.cpp"
    "${NEOMOD_SRC}/App/Neomod/GameRules.cpp"
    "${NEOMOD_SRC}/App/Neomod/SliderCurves.cpp"
    "${NEOMOD_SRC}/App/Neomod/Tools/DiffCalcSuite.cpp"
    "${NEOMOD_SRC}/App/Neomod/Tools/DiffCalcTool.cpp"
    "${NEOMOD_SRC}/Util/Parsing.cpp"
    "${NEOMOD_SRC}/Util/SString.cpp"
    "${NEOMOD_SRC}/Util/Vectors.cpp"
)

target_compile_features(diffcalc PRIVATE cxx_std_23)
target_compile_definitions(diffcalc PRIVATE BUILD_TOOLS_ONLY)
target_include_directories(diffcalc PRIVATE
    "${NEOMOD_SRC}/App/Neomod"
    "${NEOMOD_SRC}/App/Neomod/DiffCalc"
    "${NEOMOD_SRC}/Util"
)
target_link_libraries(diffcalc PRIVATE ${GLM_TARGET})
target_precompile_headers(diffcalc PRIVATE pch.h)

if(MSVC)
    string(REGEX REPLACE "/EH[^ ]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(diffcalc PRIVATE
        "/Zc:preprocessor"
        "/Zc:__cplusplus"
        "/utf-8"
        "/EHs-c-"
    )
    target_compile_definitions(diffcalc PRIVATE
        _HAS_EXCEPTIONS=0
        _UNICODE
        UNICODE=1
    )
else()
    # Disable exceptions and minimize code bloat
    target_compile_options(diffcalc PRIVATE
        -fno-exceptions
        -fno-rtti
    )

    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-fno-unwind-tables" COMPILER_SUPPORTS_NO_UNWIND_TABLES)
    if(COMPILER_SUPPORTS_NO_UNWIND_TABLES)
        target_compile_options(diffcalc PRIVATE -fno-unwind-tables)
    endif()

    check_cxx_compiler_flag("-ffunction-sections -fdata-sections" COMPILER_SUPPORTS_SECTIONS)
    if(COMPILER_SUPPORTS_SECTIONS)
        include(CheckLinkerFlag)
        check_linker_flag(CXX "LINKER:--gc-sections" LINKER_HAS_GC_SECTIONS)
        if(NOT LINKER_HAS_GC_SECTIONS)
            check_linker_flag(CXX "LINKER:-dead_strip" LINKER_HAS_DEAD_STRIP)
            if(LINKER_HAS_DEAD_STRIP)
                target_compile_options(diffcalc PRIVATE -ffunction-sections -fdata-sections)
                target_link_options(diffcalc PRIVATE LINKER:-dead_strip)
            endif()
        else()
            target_compile_options(diffcalc PRIVATE -ffunction-sections -fdata-sections)
            target_link_options(diffcalc PRIVATE LINKER:--gc-sections)
        endif()
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT TOOLCHAIN_SUPPORTS_IPO)
if(TOOLCHAIN_SUPPORTS_IPO)
    set_property(TARGET diffcalc PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
