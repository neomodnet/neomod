#!/usr/bin/env python3
# generates a compile_commands.json database for the autotools build, in two steps:
#
#   gen_compdb.py record --compiler <command> -- <wrapper> <args...>
#     stands in for CC/CXX while the objects are built: writes the compilation command to <object>.compdb (a fragment
#     in the same format as clang's -MJ), with <wrapper> (the ccache wrapper) replaced by <command>, then runs the wrapper.
#
#   gen_compdb.py [-o compile_commands.json] [--strip-pch] [--clangd .clangd [--clangd-template .clangd-template]] <fragment>...
#     combines the fragments into the database. emcc/em++ commands are rewritten to the clang commands emcc runs
#     underneath (clang tooling can't drive emcc itself), and with --clangd the CompilationDatabase of that clangd config
#     is pointed at the output's directory, creating the config from the template if it doesn't exist yet.

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

# options that emcc consumes itself instead of passing them on to clang (besides the -sSETTING[=value] ones)
EMSCRIPTEN_SETTING = re.compile(r"^-s[A-Z][A-Z0-9_]*(=.*)?$")
EMSCRIPTEN_ONLY_OPTIONS = ("-gsource-map", "-gseparate-dwarf")


def record(argv):
    parser = argparse.ArgumentParser(prog="gen_compdb.py record")
    parser.add_argument("--compiler", required=True, help="command to record instead of the wrapper")
    parser.add_argument("command", nargs="+", help="wrapper and its arguments")
    args = parser.parse_args(argv)

    output = None
    for i, arg in enumerate(args.command):
        if arg == "-o" and i + 1 < len(args.command):
            output = args.command[i + 1]
        elif arg.startswith("-o") and len(arg) > 2:
            output = arg[2:]
    # clang tooling locates the toolchain relative to the directory of argv[0] without following symlinks (e.g.
    # homebrew's /opt/homebrew/bin/x86_64-w64-mingw32-g++ would make it look for the sysroot under /opt/homebrew),
    # so record the compiler from the directory its real binary lives in, keeping the invoked name (which is what
    # the driver mode and target get inferred from)
    compiler = shlex.split(args.compiler)
    path = shutil.which(compiler[0]) or compiler[0]
    compiler[0] = os.path.join(os.path.dirname(os.path.realpath(path)), os.path.basename(path))
    # the source file is the last argument, in all of automake's compile rules and in the pch rule
    source = args.command[-1]
    if output and "-c" in args.command and not source.startswith("-"):
        entry = {
            "directory": os.getcwd(),
            "arguments": compiler + args.command[1:],
            "file": source,
            "output": output,
        }
        with open(output + ".compdb", "w") as f:
            f.write(json.dumps(entry) + ",\n")
    sys.exit(subprocess.call(args.command))


def strip_pch_args(arguments):
    """remove -include <...>/pch.h pairs"""
    filtered = []
    skip_next = False
    for i, arg in enumerate(arguments):
        if skip_next:
            skip_next = False
            continue
        if arg == "-include" and i + 1 < len(arguments) and arguments[i + 1].endswith("/pch.h"):
            skip_next = True
            continue
        filtered.append(arg)
    return filtered


def emscripten_clang_arguments(arguments, cache):
    """replace emcc/em++ with the clang it would run (em-config LLVM_ROOT) plus the flags it adds for the given
    settings (emcc --cflags), and drop the options only emcc understands"""
    driver = arguments[0]
    per_tu = {"-o", "-MF", "-MT", "-MQ"}
    settings = []
    skip_next = False
    for arg in arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        if arg in per_tu:
            skip_next = True
        elif arg in ("-c", "-MD", "-MMD", "-MP") or arg.startswith("-o") or arg.startswith("-MF") or arg.startswith("-MT"):
            pass
        elif arg.startswith("-"):
            settings.append(arg)
    key = (driver, tuple(settings))
    if key not in cache:
        em_config = os.path.join(os.path.dirname(driver), "em-config")
        llvm_root = subprocess.check_output([em_config, "LLVM_ROOT"], text=True).strip()
        clang = os.path.join(llvm_root, "clang++" if os.path.basename(driver) == "em++" else "clang")
        cflags = shlex.split(subprocess.check_output([driver, "--cflags"] + settings, text=True))
        cache[key] = [clang] + cflags

    rest = []
    skip_next = False
    for arg in arguments[1:]:
        if skip_next:
            skip_next = False
            continue
        if arg == "-s":
            skip_next = True
        elif EMSCRIPTEN_SETTING.match(arg) or arg.startswith(EMSCRIPTEN_ONLY_OPTIONS):
            pass
        else:
            rest.append(arg)
    return cache[key] + rest


def update_clangd(path, template, compdb_dir):
    rel = os.path.relpath(compdb_dir, os.path.dirname(os.path.abspath(path)))
    if os.path.exists(path):
        with open(path) as f:
            text = f.read()
    elif template:
        with open(template) as f:
            text = f.read()
    else:
        text = "CompileFlags:\n"

    line = f"    CompilationDatabase: {rel}"
    text, n = re.subn(r"^(\s*)CompilationDatabase:.*$", lambda m: f"{m.group(1)}CompilationDatabase: {rel}", text, flags=re.M)
    if n == 0:
        text, n = re.subn(r"^CompileFlags:[ \t]*\n", f"CompileFlags:\n{line}\n", text, count=1, flags=re.M)
    if n == 0:
        text = text.rstrip("\n") + f"\n\nCompileFlags:\n{line}\n"
    with open(path, "w") as f:
        f.write(text)
    print(f"{path}: CompilationDatabase: {rel}", file=sys.stderr)


def combine(argv):
    parser = argparse.ArgumentParser(description="combine compile_commands.json fragments into a database")
    parser.add_argument("fragments", nargs="+", help="fragment files (clang -MJ format)")
    parser.add_argument("-o", "--output", default="compile_commands.json", help="output file (default: compile_commands.json)")
    parser.add_argument("--strip-pch", action="store_true",
                        help="drop -include <...>/pch.h, so that clangd doesn't consider everything the pch includes as already included")
    parser.add_argument("--clangd", help="clangd config whose CompilationDatabase gets pointed at the output's directory")
    parser.add_argument("--clangd-template", help="template for --clangd if it doesn't exist yet")
    args = parser.parse_args(argv)

    entries = []
    missing = []
    emscripten_cache = {}
    for path in args.fragments:
        try:
            with open(path) as f:
                entry = json.loads(f.read().rstrip().rstrip(","))
        except FileNotFoundError:
            missing.append(path)
            continue
        except (json.JSONDecodeError, OSError) as e:
            sys.exit(f"error: {path}: {e}")
        arguments = entry["arguments"]
        if os.path.basename(arguments[0]) in ("emcc", "em++"):
            arguments = emscripten_clang_arguments(arguments, emscripten_cache)
        if args.strip_pch:
            arguments = strip_pch_args(arguments)
        entry["arguments"] = arguments
        entries.append(entry)

    if missing:
        print("error: missing fragments (were the objects built through `make compile-commands`?):", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        sys.exit(1)

    with open(args.output, "w") as f:
        json.dump(entries, f, indent=2)
        f.write("\n")
    print(f"wrote {len(entries)} entries to {args.output}", file=sys.stderr)

    if args.clangd:
        update_clangd(args.clangd, args.clangd_template, os.path.dirname(os.path.abspath(args.output)))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "record":
        record(sys.argv[2:])
    else:
        combine(sys.argv[1:])
