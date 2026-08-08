# diffcalc

Standalone PP/star rating calculator for `.osu` files, sharing the exact difficulty calculation
sources with the main game (`BUILD_TOOLS_ONLY` build, no engine). Also reachable from the main
binary via `neomod -diffcalc <same arguments>` (dispatched before any engine init, so convars
are at their defaults; the defaults are shared with this build through
`src/App/Neomod/OsuConVars/DiffCalcDefaults.h`).

The test fixtures and their golden values live in `tests/` next to this file (`tests/maps/`,
`tests/golden/`).

## Building

```sh
cmake -S . -B build
cmake --build build --parallel
```

The binary ends up at `./build/diffcalc`. GLM is found via `find_package` or fetched
automatically.

## Single map

```sh
./build/diffcalc map.osu [speed] [0xMODFLAGS | HD,HR,...]      # human readable text
./build/diffcalc map.osu 1.5 HD,HR --json                      # one JSON line
./build/diffcalc map.osu --json --dump-strains                 # + full per-section strain arrays
```

Text output is stable across versions (it is byte-compared against old builds during refactors).
JSON floats use shortest-round-trip formatting (`std::to_chars`): string equality is bit
equality. Schema (stable key order): identity (`schema`/`algo`/`map`/`mods`/`flags`/`speed`),
`beatmap` (parsed settings + counts + combo + length), `stars`, `attrs` (all
`DifficultyAttributes` fields, exhaustively - adding a field without serializing it is a compile
error), `raw` (pre-transform difficulty values), `strains` (count/sum/max digests per skill),
`pp` (four deterministic score states: `ss`, `imperfect`, `lowAcc`, `mcosuImperfect`),
`ssParams` (the post-calc `PPv2CalcParams` of the SS state). Failed maps produce an identity
line with an `"error"` field instead.

## Batch corpus mode

```sh
./build/diffcalc batch --list corpus.txt -o out.jsonl [--jobs 8]
./build/diffcalc batch --dir extracted-maps/ -o out.jsonl
find maps -name '*.osu' | sort | ./build/diffcalc batch - -o out.jsonl
```

One JSON line per (map, mods, speed); default matrix is `--mods NM,HD,HR,EZ,HDHR
--speeds 0.75,1.0,1.5`. Mod combos are concatenated two-letter codes (rates are not mods here:
DT/NC/HT are rejected, use `--speeds`). Output order is deterministic and independent of
`--jobs` (workers set per-thread FTZ/DAZ like the game does, one map never spans threads), so
two runs from the same input list compare with plain `diff`/`cmp`. Per-map load errors become
error lines in place, keeping diffs aligned; batch exits 0 on map errors, nonzero only for
harness failures.

### Corpus workflow (bit-exactness for refactors)

The golden suite is small; refactors that must not change values are additionally verified over
a large local corpus (not in the repo). One-time extraction from a directory of `.osz` sets:

```sh
find "/path/to/osz-collection" -name '*.osz' -print0 | xargs -0 -P 8 -n 1 sh -c \
  'unzip -j -o -qq "$0" "*.osu" -d "corpus/$(basename "$0" .osz)"'
find corpus -name '*.osu' | LC_ALL=C sort > corpus.txt
```

Then before the change: `diffcalc batch --list corpus.txt -o baseline.jsonl`; after:
`diffcalc batch --list corpus.txt -o after.jsonl && cmp baseline.jsonl after.jsonl`. Same
compiler and build config for both runs.

## Golden test suite

Run from the repo root (the default `--suite` is `tools/diffcalc/tests`):

```sh
./tools/diffcalc/build/diffcalc test                    # exact compare (default)
./tools/diffcalc/build/diffcalc test --tolerance 1e-9   # relative float tolerance (cross-platform/CI)
./tools/diffcalc/build/diffcalc test --record           # regenerate goldens after an intended change
```

Runs every fixture through a fixed 14-config matrix (NM/HD/HR/EZ, rates 1.5/0.75/1.25, HD,HR,
HD@1.5, RX/AP/TD, FL, HD,FL) and compares against the checked-in per-fixture goldens in
`tests/golden/`. The goldens pin the current `PP_ALGORITHM_VERSION` bit-exactly on the platform
they were recorded on (arm64 macOS); exact mode is for same-platform refactor verification,
`--tolerance` exists because float formatting is only bit-stable per platform (libm
differences, e.g. wasm differs around the 9th significant digit). Failures name the fixture,
config and dotted field: `FAIL 2785319.osu HD,HR@1 attrs.aimDifficulty: expected X got Y`.

Algorithm changes are expected to fail the suite until re-recorded together with a
`PP_ALGORITHM_VERSION` bump; the golden diff is part of the review. Two pinned semantics to
keep in mind when reading values: the tool feeds file CS/AR/OD/HP straight into the calculation
(HR/EZ only reach the pp multipliers; the in-game CS/AR/OD adjustment happens in
`BeatmapInterface`), and single-shot values are McKay pass-1 (the DB batch loops store pass-2,
which differs on >5000-slider maps).

### Fixtures (`tests/maps/`)

Real ranked maps are named `<beatmap_id>.osu`; degenerate cases are tiny hand-written files.

| fixture | what it covers | source |
|---|---|---|
| `2785319.osu` | slider-tech allrounder, 307c/293s/1sp, break, 50 SV changes, ~5.7* | MYUKKE. - re[in]flaw (captin1) [toybot's Expert], set 1344871, copied from rosu-pp's resources |
| `5069787.osu` | jump aim, 88% circles, ~6.0* | UBEL - Obgonyay (nemidnight) [smoke n truth], set 2354724 |
| `4697633.osu` | deathstreams, 2377 objects, speed-dominant | DJ Sharpnel - Entangle Night (aspen) [Entangle Stream -220-], set 2216137 |
| `2730751.osu` | 9.5* tech, 50% sliders | Camellia - M1LLI0N PP (OliBomby) [ppOverheat], set 1318077 |
| `4979024.osu` | 1.3* easy diff | Shinji Orito - Shionari (maikayuii) [Kurboh's Easygoing], set 2323420 |
| `4331955.osu` | 7000-object 30min marathon with breaks | Mario Kart Wii Compilation (Woey) [99,999 CC], set 2070329 |
| `single-circle.osu` | 1 object, hits the 0-star early return (no state stamping) | hand-written |
| `empty-objects.osu` | zero objects, "no objects in file" error line | hand-written |
| `zero-length-slider.osu` | 0-pixelLength slider with a single-point curve | hand-written |
| `negative-duration.osu` | spinner with endTime < startTime (Aspire getDuration clamp) | hand-written |
| `synthetic-5001-sliders.osu` | >5000 sliders, deferred McKay curve alloc path | `python3 gen_5001_sliders.py` (deterministic, checked in) |

Real fixtures were extracted from local `.osz` archives with:

```sh
unzip -p "<set>.osz" "<Artist - Title (Creator) [Diff]>.osu" > tests/maps/<beatmap_id>.osu
```

Adding a fixture: drop the `.osu` into `tests/maps/`, run `test --record`, commit both.
Removing one: delete the `.osu` and re-record (`--record` prunes the stale golden; a plain
`test` run fails loudly on it).

## Crosscheck mode

```sh
./tools/diffcalc/build/diffcalc crosscheck [--suite <dir>]
```

Self-consistency checks over the fixtures for the reuse paths one-shot runs never exercise:
same-key `StrainComputeState` skip == fresh compute, key mismatch (OD/autopilot) triggers a
correct recompute over the used vector, growing-prefix `upToObjectIndex` calcs == fresh
truncated calcs (the live pp pattern), and recompute (pass >= 2) stability. On >5000-slider
maps pass-1 defers some slider curve allocations, so pass-1 is only ever compared against
pass-1; the pass-1 vs pass-2 relation is reported as INFO there (do not "fix" the deferred
alloc leftovers without a version bump).
