#!/usr/bin/env bash

BIN="${BIN:-"/opt/msvc/bin/x64"}"

export PATH="${BIN}:${PATH}"

ENV="$BIN/msvcenv.sh"
if [ ! -f "$ENV" ]; then
	printf '%s' "$ENV doesn't exist, install msvc headers and set BIN" && exit 1
else
	export INCLUDE="$(bash -c ". $ENV && /bin/echo \"\$INCLUDE\"" | sed s/z://g | sed 's/\\/\//g')"
	export LIB="$(bash -c ". $ENV && /bin/echo \"\$LIB\"" | sed s/z://g | sed 's/\\/\//g')"
	export TARGET_TRIPLE=x86_64-windows-msvc
fi

if [[ "${1:-}" = *debug* ]]; then BUILD_TYPE="Debug"; fi

BUILD_TYPE="${BUILD_TYPE:-Release}"

BUILD_DIR=$PWD/build-msvc-"${BUILD_TYPE}"
INSTALL_DIR=$PWD/dist-"${BUILD_TYPE}"

export PKG_CONFIG_PATH="${BUILD_DIR}/deps/install/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="${BUILD_DIR}/deps/install/lib/pkgconfig"
export PKG_CONFIG="pkgconf --msvc-syntax"

export CC=cl CXX=cl

WINE="${WINE:-wine}"
WINESERVER="${WINESERVER:-wineserver}"

export WINEPREFIX=$PWD/.winepfx

export WINEBUILD=1

# mkdir -p "$WINEPREFIX/dosdevices/" # shorten paths
# ln -s "$(realpath "$PWD"/../)" "$WINEPREFIX/dosdevices/d:" 2>/dev/null

exit_cleanup() {
	$WINESERVER -k9
	# these just stay alive forever as zombies due to a known wine bug w/ MSVC and parallel jobs :/
	for pid in $(pgrep -wf 'cl.exe'); do kill -SIGKILL "$pid"; done
}

doit() {
	$WINESERVER -k # kill a potential old server
	$WINESERVER -p # start a new server
	$WINE wineboot # run a process to start up all background wine processes

	mkdir -p "$INSTALL_DIR" "$BUILD_DIR" &&
		cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" \
			-DCMAKE_CXX_FLAGS="/DWIN32 /D_WINDOWS /EHs-c-" \
			-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
			-DCMAKE_C_OUTPUT_EXTENSION=obj \
			-DCMAKE_CXX_OUTPUT_EXTENSION=obj \
			-DCMAKE_C_COMPILER="cl" \
			-DCMAKE_CXX_COMPILER="cl" \
			-DCMAKE_SYSTEM_NAME=Windows \
			-DCMAKE_SYSTEM_PROCESSOR=AMD64 \
			-DCMAKE_FIND_ROOT_PATH="${BUILD_DIR}/deps/install" \
			-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
			-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
			-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
			-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
			-DCMAKE_HOST_SYSTEM_PROCESSOR=AMD64 -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DWINEBUILD=ON &&

		# Build the superbuild (dependencies + initial project configuration)
		cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(nproc)"
		# ignore errors, the above might break if source files were changed (they aren't dependency tracked)
		# Now build the main project directly to ensure source changes are detected
		cmake --build "$BUILD_DIR/neomod" --config "$BUILD_TYPE" --parallel "$(nproc)" &&
		# and install it
		cmake --install "$BUILD_DIR/neomod" --config "$BUILD_TYPE"
}

trap exit_cleanup EXIT

doit || true

exit_cleanup
