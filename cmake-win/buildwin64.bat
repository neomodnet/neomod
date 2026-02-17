@echo off
setlocal

:: Check for Visual Studio environment
if "%VSCMD_VER%"=="" (
    echo Error: Run this from a Visual Studio Developer Command Prompt
    echo Use "x64 Native Tools Command Prompt for VS" or run vcvars64.bat
    exit /b 1
)

:: Check for pkgconf (required for proper MSVC syntax support)
echo Checking for pkgconf...
pkg-config --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo pkg-config not found, installing pkgconf via pip...
    goto install_pkgconf
)

:: Get version and check if it's pkgconf (2.x) rather than old pkg-config
for /f "tokens=*" %%i in ('pkg-config --version 2^>nul') do set PKG_VERSION=%%i
echo Found pkg-config version: %PKG_VERSION%

:: Extract major version number
for /f "tokens=1 delims=." %%a in ("%PKG_VERSION%") do set MAJOR=%%a

:: Check if major version is less than 2
if "%MAJOR%" LSS "2" (
    echo "pkg-config version too old (need pkgconf 2.x), installing pkgconf..."
    goto install_pkgconf
)

echo pkgconf version is acceptable
goto continue_build

:install_pkgconf
pip install pkgconf
if %ERRORLEVEL% neq 0 (
    echo Failed to install pkgconf via pip
    echo Please install pkgconf manually: pip install pkgconf
    exit /b 1
)
echo pkgconf installed successfully

:continue_build

:: Default build directory
set BUILD_DIR="%cd%\build-msvc-release"
set BUILD_TYPE=Release

set JOBS="8"

:: Handle command line arguments
:parse_args
if "%1"=="--debug" (
    set BUILD_TYPE=Debug
    set BUILD_DIR="%cd%\build-msvc-debug"
    shift
    goto parse_args
)
if "%1"=="--clean" (
    set CLEAN=1
    shift
    goto parse_args
)
if "%1"=="--clean-cache" (
    echo Cleaning dependency cache...
    if exist "depcache" rmdir /s /q "depcache"
    shift
    goto parse_args
)
if "%1"=="--jobs" (
    shift
    if not "%1"=="" (
        set JOBS="%1"
    )
    echo Using %JOBS% parallel build tasks
    shift
    goto parse_args
)
if "%1"=="--help" (
    echo Usage: build.bat [options]
    echo Options:
    echo   --clean       Clean build directory before building
    echo   --clean-cache Clean dependency cache
    echo   --debug       Build debug configuration instead of release
    echo   --jobs n      i.e. make -j
    echo   --help        Show this help
    exit /b 0
)
if not "%1"=="" (
    echo Unknown argument: %1
    echo Use --help for usage information
    exit /b 1
)

if not "%CLEAN%"=="" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rmdir /s /q "%cd%\%BUILD_DIR%"
)

set INSTALL_PREFIX="%cd%\dist-%BUILD_TYPE%"

:: Show build configuration
echo Building neomod (%BUILD_TYPE% configuration) using superbuild
echo Build directory: %BUILD_DIR%
echo Dependency cache: depcache
echo.

:: Check for LivePP
if exist "..\LivePP" (
    set WITH_LIVEPP=ON
) else (
    set WITH_LIVEPP=OFF
)

:: Configure superbuild
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%INSTALL_PREFIX%" mkdir "%INSTALL_PREFIX%"
echo Configuring superbuild...
cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_PREFIX%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DWITH_LIVEPP=%WITH_LIVEPP% ^
    -DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHs-c-"
if %ERRORLEVEL% neq 0 (
    echo Configuration failed
    exit /b 1
)

:: Build superbuild (dependencies + initial project configuration)
echo Building superbuild...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %JOBS%
if %ERRORLEVEL% neq 0 (
    echo Build failed
    exit /b 1
)

:: Build main project directly to ensure source changes are detected
echo Building main project...
cmake --build "%BUILD_DIR%\neomod" --config %BUILD_TYPE% --parallel %JOBS%
if %ERRORLEVEL% neq 0 (
    echo Main project build failed
    exit /b 1
)

echo Installing main project...
cmake --install "%BUILD_DIR%\neomod" --config %BUILD_TYPE%
if %ERRORLEVEL% neq 0 (
    echo Main project install failed
    exit /b 1
)

echo.
echo Build completed successfully!
echo Executable: %INSTALL_PREFIX%\bin\neomod.exe
echo Dependencies cached in: depcache (reused across builds)
