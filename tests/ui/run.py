#!/usr/bin/env python3
"""headless UI test runner: pipes each scripts/*.txt into the binary, checks UITEST
assertions and (when a golden exists) diffs the uitrace event log.

usage:
  run.py [script-name ...]        run the named scripts (no args = run all)
  run.py --record [...]           (re)record golden traces instead of checking
  run.py --bin /path/to/neomod    use a specific binary

the binary is autodiscovered as the most recently built <repo>/*/dist/bin-*/neomod;
--bin or the NEOMOD_BIN env var override it. --record or RECORD_GOLDEN=1 records goldens.
"""

import argparse
import difflib
import os
import re
import shlex
import shutil
import subprocess
import sys
import wave
import zipfile
from pathlib import Path

import pixelprobe

TESTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TESTS_DIR.parents[1]
SCRIPTS_DIR = TESTS_DIR / "scripts"
GOLDEN_DIR = TESTS_DIR / "golden"
PROBES_DIR = TESTS_DIR / "probes"
OUT_DIR = TESTS_DIR / "out"


def find_binary(explicit):
    """resolve the neomod binary: explicit override, else most recently built one."""
    if explicit:
        p = Path(explicit).expanduser()
        if not (p.is_file() and os.access(p, os.X_OK)):
            sys.exit(f"error: binary not found or not executable: {p}")
        return p
    candidates = sorted(
        (p for p in REPO_ROOT.glob("*/dist/bin-*/neomod") if os.access(p, os.X_OK)),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        sys.exit(
            f"error: no neomod binary under {REPO_ROOT}/*/dist/bin-*/ "
            "(build one, set NEOMOD_BIN, or pass --bin)"
        )
    chosen = candidates[0]
    note = "" if len(candidates) == 1 else f" (most recent of {len(candidates)}; --bin to override)"
    print(f"using {chosen.relative_to(REPO_ROOT)}{note}")
    return chosen


def beatmap(title, version, audio, n_objects, set_id=-1):
    """minimal v14 .osu: metadata + n_objects evenly-spaced circles (500ms apart)."""
    lines = [
        "osu file format v14",
        "",
        "[General]",
        f"AudioFilename: {audio}",
        "Mode: 0",
        "",
        "[Metadata]",
        f"Title:{title}",
        "Artist:UITest",
        "Creator:uitest",
        f"Version:{version}",
        "BeatmapID:0",
        f"BeatmapSetID:{set_id}",
        "",
        "[Difficulty]",
        "HPDrainRate:5",
        "CircleSize:4",
        "OverallDifficulty:5",
        "ApproachRate:5",
        "SliderMultiplier:1.4",
        "SliderTickRate:1",
        "",
        "[TimingPoints]",
        "0,500,4,2,0,100,1,0",
        "",
        "[HitObjects]",
    ]
    t = 1000
    for _ in range(n_objects):
        lines.append(f"256,192,{t},1,0,0:0:0:0:")
        t += 500
    return "\n".join(lines) + "\n"


def maps_set(bindir, folder, set_id, n_diffs):
    """a raw beatmapset folder in the binary's maps/ drop-zone (metadata-only diffs), for the
    '# uitest-maps:' directive; run_one removes it again after the script."""
    d = bindir / "maps" / folder
    d.mkdir(parents=True, exist_ok=True)
    for i in range(1, n_diffs + 1):
        (d / f"diff{i:02d}.osu").write_text(
            beatmap(f"UITest {folder}", f"diff {i:02d}", "none.mp3", i, set_id)
        )
    return d


def script_directives(text):
    """'# uitest-<key>: <value>' lines in a script's leading comment block:
    maps: '<folder>|<set_id>|<n_diffs>' provisions maps/<folder>/ before the run (removed after)
    args: extra binary argv (e.g. an .osz path relative to the bin dir, imported like a drag/drop)
    cleanup: a maps/<folder> the script is expected to create, removed after the run"""
    out = {}
    for ln in text.splitlines():
        if not ln.startswith("#"):
            break
        m = re.match(r"#\s*uitest-(\w+):\s*(.*)", ln)
        if m:
            out.setdefault(m.group(1), []).append(m.group(2).strip())
    return out


def provision_fixtures(bindir):
    """create the test fixtures next to the binary (assets are looked up relative to it)."""
    # the skins dropdown only opens if at least one skin folder exists
    # (skins_dropdown_hover relies on the deterministic "default" + "UITestSkin" item pair);
    # the empty osu folder keeps songbrowser db loads empty and machine-independent
    (bindir / "skins" / "UITestSkin").mkdir(parents=True, exist_ok=True)
    (bindir / "uitest_osu_folder").mkdir(parents=True, exist_ok=True)

    # carousel fixture: ONE beatmapset with 20 diffs makes the carousel scrollable AND
    # keeps the boot selection deterministic (selectRandomBeatmap skips prand() for a single
    # set), so goldens stay stable without seeding the rng. metadata only (the referenced
    # audio file deliberately doesn't exist)
    full_songs = bindir / "uitest_osu_folder_full" / "Songs" / "uitest set"
    if not full_songs.is_dir():
        full_songs.mkdir(parents=True)
        for i in range(1, 21):
            (full_songs / f"diff{i:02d}.osu").write_text(
                beatmap("UITest Map", f"diff {i:02d}", "none.mp3", i)
            )

    # play-mode fixture: ONE set with ONE PLAYABLE map. unlike the carousel fixture above, this
    # has REAL (silent) audio so the map actually starts instead of hanging forever on
    # "Loading..." (a missing/empty AudioFilename never resolves). ~30s of evenly-spaced circles
    # gives a wide mid-play window for the play-mode pins; single set+diff keeps the boot
    # selection deterministic. the pins run mod_nofail 1 so missed circles don't end the map.
    play_songs = bindir / "uitest_osu_folder_play" / "Songs" / "play set"
    if not play_songs.is_dir():
        play_songs.mkdir(parents=True)
        # 35s of 8kHz 16-bit mono PCM silence (covers the whole map; SoLoud decodes WAV)
        with wave.open(str(play_songs / "silence.wav"), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(8000)
            w.writeframes(b"\x00\x00" * (8000 * 35))
        (play_songs / "play.osu").write_text(beatmap("UITest Play", "play", "silence.wav", 60))

    # .osz import fixtures (passed to the binary via '# uitest-args:', i.e. the drag/drop path):
    # a set with an id, the same set with one more diff (merges into the installed folder), and a
    # set without any id (unsubmitted-style, named after the file)
    imports = bindir / "uitest_import"
    if not imports.is_dir():
        imports.mkdir()
        for name, set_id, diffs, title in (
            ("set123.osz", 123, (1, 2), "UITest Set123"),
            ("set123_extra.osz", 123, (1, 2, 3), "UITest Set123"),
            ("idless.osz", -1, (1, 2), "UITest Idless"),
        ):
            with zipfile.ZipFile(imports / name, "w", zipfile.ZIP_DEFLATED) as z:
                for i in diffs:
                    z.writestr(f"diff{i:02d}.osu", beatmap(title, f"diff {i:02d}", "none.mp3", i, set_id))


def run_one(name, binary, bindir, record):
    """run one script; print its result line(s); return True if it passed/recorded."""
    script = SCRIPTS_DIR / f"{name}.txt"
    log_path = OUT_DIR / f"{name}.log"
    out_trace = OUT_DIR / f"{name}.trace"
    diff_path = OUT_DIR / f"{name}.trace.diff"
    golden = GOLDEN_DIR / f"{name}.trace"
    shot = bindir / "screenshots" / f"uitest_{name}.png"
    shot.unlink(missing_ok=True)

    # binary must run from its install dir (assets are relative). ui_validate_ticks is injected
    # into the script's frame-0 batch (same frame as the preamble, so traces don't shift): every
    # screen must be ticked every frame (debug builds)
    text = script.read_text()
    directives = script_directives(text)
    fixture_dirs = []
    for spec in directives.get("maps", []):
        folder, set_id, n_diffs = (s.strip() for s in spec.split("|"))
        fixture_dirs.append(maps_set(bindir, folder, int(set_id), int(n_diffs)))
    extra_args = [a for spec in directives.get("args", []) for a in shlex.split(spec)]

    proc = subprocess.run(
        [f"./{binary.name}", "-headless", *extra_args],
        cwd=bindir,
        input="ui_validate_ticks 1\n" + text,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    log = proc.stdout

    # maps/ fixtures and imports must not leak into the next script's carousel (the db's startup
    # pass drops their records on the next boot)
    for d in fixture_dirs:
        shutil.rmtree(d, ignore_errors=True)
    for folder in directives.get("cleanup", []):
        shutil.rmtree(bindir / "maps" / folder, ignore_errors=True)

    reasons = []
    if proc.returncode != 0:
        reasons.append(f"crash(rc={proc.returncode})")
    if "UITEST FAIL" in log:
        reasons.append("assert")

    trace = "".join(ln + "\n" for ln in log.splitlines() if ln.startswith("uitrace "))
    out_trace.write_text(trace)

    if record:
        if reasons:
            log_path.write_text(log)
            print(f"RECORD FAILED {name} ({' '.join(reasons)}) -- golden NOT updated, see {log_path}")
            return False
        GOLDEN_DIR.mkdir(exist_ok=True)
        golden.write_text(trace)
        log_path.write_text(log)
        print(f"RECORDED {name} ({trace.count(chr(10))} trace lines)")
        return True

    if golden.is_file():
        diff = list(
            difflib.unified_diff(
                golden.read_text().splitlines(keepends=True),
                trace.splitlines(keepends=True),
                fromfile=str(golden),
                tofile=str(out_trace),
            )
        )
        if diff:
            reasons.append("trace-diff")
            diff_path.write_text("".join(diff))
        else:
            diff_path.unlink(missing_ok=True)

    # pixel probes (phase 3+ draw-order checks): probes/<name>.probes lines are
    # "<relx> <rely> <rrggbb> <tol>" (# = comment), checked against the script's
    # take_screenshot capture at screenshots/uitest_<name>.png; results are appended to the log
    probes = PROBES_DIR / f"{name}.probes"
    if probes.is_file():
        if not shot.is_file():
            reasons.append("no-screenshot")
        else:
            for line in probes.read_text().splitlines():
                parts = line.split()
                if not parts or parts[0].startswith("#"):
                    continue
                px, py, color, tol = parts[:4]
                ok, message = pixelprobe.probe(str(shot), float(px), float(py), color, int(tol))
                log += message + "\n"
                if not ok and "probe" not in reasons:
                    reasons.append("probe")

    log_path.write_text(log)

    if not reasons:
        print(f"PASS {name}")
        return True

    print(f"FAIL {name} ({' '.join(reasons)}) -- see {log_path}")
    for ln in log.splitlines():
        if "UITEST FAIL" in ln or "PROBE FAIL" in ln:
            print(f"    {ln}")
    if diff_path.is_file():
        for ln in diff_path.read_text().splitlines()[:15]:
            print(f"    {ln}")
    return False


def main():
    ap = argparse.ArgumentParser(description="headless UI test runner")
    ap.add_argument("scripts", nargs="*", help="script names without .txt (default: all)")
    ap.add_argument(
        "-r", "--record", action="store_true", help="record golden traces instead of checking"
    )
    ap.add_argument("--bin", help="neomod binary path (default: autodiscover; or set NEOMOD_BIN)")
    args = ap.parse_args()

    record = args.record or os.environ.get("RECORD_GOLDEN") == "1"
    binary = find_binary(args.bin or os.environ.get("NEOMOD_BIN"))
    bindir = binary.parent

    OUT_DIR.mkdir(exist_ok=True)
    provision_fixtures(bindir)

    if args.scripts:
        names = args.scripts
        missing = [n for n in names if not (SCRIPTS_DIR / f"{n}.txt").is_file()]
        if missing:
            sys.exit(f"error: no such script(s): {', '.join(missing)}")
    else:
        names = sorted(p.stem for p in SCRIPTS_DIR.glob("*.txt"))

    passed = sum(run_one(name, binary, bindir, record) for name in names)
    failed = len(names) - passed

    print("----")
    print(f"{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
