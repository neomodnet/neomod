#!/usr/bin/env python3
"""sanity test for a finished build: boots it headless on a fresh data dir, plays a generated map on autoplay
and quits (see run_args.txt). meant for CI, to catch builds that crash, hang or don't work at all on a platform.

usage:
  run.py <neomod executable> [-- extra launch args, e.g. -opengl]
  run.py <neomod.js>         the wasm build, run with node (which has no renderer, so there's no screenshot)

a run passes if every ui_assert in run_args.txt passes, the game shuts down cleanly with exit code 0 and its
screenshot of the ranking screen isn't blank. the data dir is kept if the run fails.
"""

import argparse
import math
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import wave
import zlib
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "run_args.txt"

# circles, a slider and a spinner over ~5 s
BEATMAP = """osu file format v14

[General]
AudioFilename: audio.wav
AudioLeadIn: 0
Mode: 0

[Metadata]
Title:Sanity Test
Artist:neomod
Creator:neomod
Version:Normal
BeatmapID:0
BeatmapSetID:-1

[Difficulty]
HPDrainRate:5
CircleSize:4
OverallDifficulty:8
ApproachRate:9
SliderMultiplier:1.4
SliderTickRate:1

[TimingPoints]
0,500,4,2,0,100,1,0

[HitObjects]
64,96,1000,5,0,0:0:0:0:
192,96,1500,1,0,0:0:0:0:
64,256,2000,2,0,L|320:256,1,256
448,192,3000,1,0,0:0:0:0:
256,192,3500,12,0,4500,0:0:0:0:
256,192,5000,5,0,0:0:0:0:
"""


def write_data_dir(data, game_data):
    """a config and one playable beatmapset in maps/, which the song browser's first database load picks up.
    game_data is the data dir's path as the game sees it (the wasm build has a virtual file system)."""
    (data / "cfg").mkdir(parents=True)
    (data / "osu").mkdir()
    # an empty osu! folder, so the maps of an installed osu! don't get loaded (and played) instead
    (data / "cfg" / "osu.cfg").write_text(
        f"auto_update 0\nosu_folder {game_data}/osu\nwindowed_resolution 1280x720\n"
    )
    songs = data / "maps" / "Sanity Test"
    songs.mkdir(parents=True)
    (songs / "Normal.osu").write_text(BEATMAP)
    # 6 s of a 441 Hz sine (audio processing skips over silence)
    with wave.open(str(songs / "audio.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(22050)
        period = struct.pack("<50h", *(round(8000 * math.sin(2 * math.pi * i / 50)) for i in range(50)))
        w.writeframes(period * (22050 * 6 // 50))


def count_colors(png):
    """(width, height, number of distinct colors) of an 8-bit rgb/rgba png without interlacing, which is what
    take_screenshot writes"""
    data = png.read_bytes()
    pos, idat = 8, []
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos : pos + 8])
        if tag == b"IHDR":
            width, height, _, color_type = struct.unpack(">IIBB", data[pos + 8 : pos + 18])
        elif tag == b"IDAT":
            idat.append(data[pos + 8 : pos + 8 + length])
        pos += 12 + length
    bpp = 4 if color_type == 6 else 3
    raw = zlib.decompress(b"".join(idat))
    stride = width * bpp
    prev = bytearray(stride)
    colors = set()
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1 : start + 1 + stride])
        # undo the row's filter (none, sub, up, average, paeth)
        if kind == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:
            line = bytearray((x + b) & 0xFF for x, b in zip(line, prev))
        elif kind == 3:
            for i in range(stride):
                line[i] = (line[i] + ((line[i - bpp] if i >= bpp else 0) + prev[i]) // 2) & 0xFF
        elif kind == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        colors.update(bytes(line[x : x + 3]) for x in range(0, stride, bpp))
        prev = line
    return width, height, len(colors)


def describe_exit(rc):
    if rc < 0:  # killed by a signal
        try:
            return f"{rc} ({signal.Signals(-rc).name})"
        except ValueError:
            return str(rc)
    return f"{rc:#x}" if rc > 255 else str(rc)  # windows exception codes read better in hex


def main():
    ap = argparse.ArgumentParser(
        usage="%(prog)s [-h] [--timeout SECS] [-v] target [-- launch args]",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    ap.add_argument("target", type=Path, help="the neomod executable, or neomod.js of the wasm build")
    ap.add_argument("--timeout", type=float, default=120, metavar="SECS", help="when the run counts as hung")
    ap.add_argument("-v", "--verbose", action="store_true", help="print the game's output on success too")
    # everything after -- goes to the game (split off by hand, argparse can't mix that with options on 3.9)
    argv = sys.argv[1:]
    split = argv.index("--") if "--" in argv else len(argv)
    args = ap.parse_args(argv[:split])
    launch_args = argv[split + 1 :]
    sys.stdout.reconfigure(errors="replace")  # the game's output may not fit the console's encoding

    target = args.target.resolve()
    wasm = target.suffix == ".js"
    if wasm:
        # neomod.js mounts neomod-data/ next to itself as /persist
        data = target.parent / "neomod-data" / "sanity"
        shutil.rmtree(data, ignore_errors=True)
        game_data = "/persist/sanity"
        cmd = ["node", str(target)]
    else:
        data = Path(tempfile.mkdtemp(prefix="neomod-sanity-"))
        game_data = data.as_posix()
        cmd = [str(target)]
    write_data_dir(data, game_data)

    # -multi: otherwise an instance that's already running would get the launch args and this one would quit
    cmd += ["-headless", "-multi", "-datadir", game_data, *launch_args]
    script = [ln for ln in SCRIPT.read_text().splitlines() if ln and not ln.startswith("#")]
    print("running:", " ".join(cmd), flush=True)
    start = time.monotonic()
    try:
        proc = subprocess.run(
            cmd,
            cwd=target.parent,  # node looks for neomod.data in the working directory
            input="".join(ln + "\n" for ln in script),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
            errors="replace",
            timeout=args.timeout,
        )
        output, rc = proc.stdout, proc.returncode
    except subprocess.TimeoutExpired as e:
        output = e.output.decode("utf-8", "replace") if isinstance(e.output, bytes) else e.output or ""
        rc = None
    elapsed = time.monotonic() - start

    problems = []
    if rc is None:
        problems.append(f"hung (killed after {args.timeout:g} s)")
    elif rc != 0:
        problems.append(f"exit code {describe_exit(rc)}")
    elif "Shutdown success." not in output:
        problems.append("no clean shutdown")
    lines = output.splitlines()
    problems += [ln for ln in lines if "UITEST FAIL" in ln or "Unknown command:" in ln]
    expected = sum(ln.startswith("ui_assert") for ln in script)
    passed = sum("UITEST OK" in ln for ln in lines)
    if passed != expected:
        problems.append(f"{passed} of {expected} asserts passed")
    shot = data / "screenshots" / "sanity.png"
    summary = f"{passed} asserts"
    if not wasm:
        try:
            width, height, colors = count_colors(shot)
        except Exception as e:  # missing or broken
            problems.append(f"no usable screenshot ({e})")
        else:
            summary += f", {width}x{height} screenshot with {colors} colors"
            if colors < 256:
                problems.append(f"the screenshot looks blank ({colors} colors)")

    if problems or args.verbose:
        print(output)
    if problems:
        print(f"FAIL after {elapsed:.1f} s (data dir: {data})")
        for p in problems:
            print(f"  {p}")
        return 1
    shutil.rmtree(data)
    print(f"PASS in {elapsed:.1f} s ({summary})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
