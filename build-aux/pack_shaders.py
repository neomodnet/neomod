#!/usr/bin/env python3
# Copyright (c) 2026, WH, All rights reserved.
# Compiles the canonical shaders into the formats each backend consumes.
#
# Finds VK_*_{v,f}.glsl in --shader-dir (the single canonical source) and
# compiles each to SPIR-V via glslc or glslangValidator. From that SPIR-V,
# spirv-cross transpiles the per-backend formats. Three independent outputs,
# all selectable via their --manifest flag (at least one is required):
#   --manifest      SDL_gpu .shdpk packs (GLSL source + SPIR-V + HLSL->DXIL + MSL)
#   --dx11-manifest standalone DX11 SM5.0 .hlsl (D3DCompile'd at runtime; no dxc)
#   --gl-manifest   OpenGL .glsl in the --gl-dialect of the enabled GL backend
#                   (legacy: GLSL 1.10 reading the fixed-function vertex inputs, es: GLSL ES 1.00)
# All generated GLSL/HLSL/MSL/DXIL are derived, not authored.
#
# What the backends rely on in a canonical shader (Vulkan GLSL 450):
#   - vertex inputs are declared in full and named exactly
#       layout(location = 0) in vec3 inPos; layout(location = 1) in vec4 inCol; layout(location = 2) in vec2 inTex;
#     (DX11 reflects its input layout from them, the legacy GL remap matches them by name)
#   - uniform blocks have an instance name, "vu" in the vertex stage (set = 1) and "fu" in the fragment stage
#     (set = 3); the engine sets uniforms by member name, so member names are unique across both blocks
#   - the texture is "layout(set = 2, binding = 0) uniform sampler2D tex0;"
#   - the code stays within what GLSL 1.10 / GLSL ES 1.00 can express (spirv-cross fails the build otherwise)
#   - gl_FragCoord keeps the origin of whichever backend runs the shader (see Graphics::hasFlippedTextureOrigin())
#
# Usage:
#   pack_shaders.py --shader-dir <dir> --output-dir <dir> --manifest <file> [--glslc <path>] [--dxc <path>] [--spirv-cross <path>]
#   pack_shaders.py --shader-dir <dir> --output-dir <dir> --dx11-manifest <file> --glslc <path> --spirv-cross <path>
#   pack_shaders.py --shader-dir <dir> --output-dir <dir> --gl-manifest <file> --gl-dialect <legacy|es> --glslc <path> --spirv-cross <path>
#
# .shdpk format:
#   [4B] magic "SGSH"
#   [4B] version (1)
#   [4B] num_sections
#   Per section:
#     [4B] format tag (0=GLSL, 1=SPIRV, 2=DXIL, 3=MSL)
#     [4B] data offset from start of file
#     [4B] data size
#   Section data follows

import sys
import os
import struct
import argparse
import subprocess
import glob
import re

MAGIC = b'SGSH'
VERSION = 1

FMT_GLSL = 0
FMT_SPIRV = 1
FMT_DXIL = 2
FMT_MSL = 3


def write_shdpk(output_path, sections):
    """Write a .shdpk file from a list of (format_tag, data_bytes) tuples."""
    num_sections = len(sections)
    header_size = 12 + 12 * num_sections
    offset = header_size
    section_info = []
    for fmt_tag, data in sections:
        section_info.append((fmt_tag, offset, len(data)))
        offset += len(data)

    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(MAGIC)
        f.write(struct.pack('<I', VERSION))
        f.write(struct.pack('<I', num_sections))
        for fmt_tag, data_offset, data_size in section_info:
            f.write(struct.pack('<III', fmt_tag, data_offset, data_size))
        for _, data in sections:
            f.write(data)


def compile_hlsl(dxc, hlsl_path, dxil_path, stage):
    """Compile an HLSL file to DXIL. Returns True on success."""
    profile = 'vs_6_0' if stage == 'v' else 'ps_6_0'
    os.makedirs(os.path.dirname(dxil_path) or '.', exist_ok=True)
    cmd = [dxc, '-T', profile, '-E', 'main', '-Fo', dxil_path, hlsl_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f'dxc failed for {hlsl_path}:', file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    return True


def is_glslang_validator(glslc):
    """Check if the glslc path points to glslangValidator."""
    return os.path.basename(glslc).startswith('glslangValidator')


def compile_glsl(glslc, glsl_path, spv_path, stage):
    """Compile a GLSL file to SPIR-V. Returns True on success."""
    stage_flag = 'vert' if stage == 'v' else 'frag'
    os.makedirs(os.path.dirname(spv_path) or '.', exist_ok=True)
    if is_glslang_validator(glslc):
        cmd = [glslc, '-V', '--target-env', 'vulkan1.0', '-S', stage_flag, '-o', spv_path, glsl_path]
    else:
        cmd = [glslc, f'-fshader-stage={stage_flag}', '--target-env=vulkan1.0', '-o', spv_path, glsl_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        tool_name = 'glslangValidator' if is_glslang_validator(glslc) else 'glslc'
        print(f'{tool_name} failed for {glsl_path}:', file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    return True


def transpile_hlsl(spirv_cross, spv_path, hlsl_path, shader_model):
    """Transpile SPIR-V to HLSL via spirv-cross. Returns True on success."""
    os.makedirs(os.path.dirname(hlsl_path) or '.', exist_ok=True)
    cmd = [spirv_cross, spv_path, '--hlsl', '--shader-model', shader_model, '--output', hlsl_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f'spirv-cross (HLSL) failed for {spv_path}:', file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    return True


def transpile_msl(spirv_cross, spv_path, msl_path, stage):
    """Transpile SPIR-V to MSL via spirv-cross. Returns True on success."""
    # entry point must be main0: 'main' is reserved in MSL and SDLGPUShader expects main0
    stage_flag = 'vert' if stage == 'v' else 'frag'
    os.makedirs(os.path.dirname(msl_path) or '.', exist_ok=True)
    cmd = [spirv_cross, spv_path, '--msl', '--msl-decoration-binding',
           '--rename-entry-point', 'main', 'main0', stage_flag, '--output', msl_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f'spirv-cross (MSL) failed for {spv_path}:', file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False
    return True


# GLSL dialect of each OpenGL backend
GL_DIALECT_FLAGS = {
    'legacy': ['--no-es', '--version', '110', '--no-420pack-extension'],
    'es': ['--es', '--version', '100'],
}

# how each OpenGL backend feeds the canonical vertex inputs (location 0/1/2 = inPos/inCol/inTex):
# the legacy backend draws through the fixed-function arrays and glBegin, so its inputs are the matching built-ins
# (spirv-cross emits a remap target verbatim, hence the swizzles to keep the declared widths);
# the ES backend binds generic attributes by name
GL_VERTEX_INPUT_FLAGS = {
    'legacy': ['--remap', 'inPos', 'gl_Vertex.xyz', '3',
               '--remap', 'inCol', 'gl_Color', '4',
               '--remap', 'inTex', 'gl_MultiTexCoord0.xy', '2'],
    'es': ['--rename-interface-variable', 'in', '0', 'position',
           '--rename-interface-variable', 'in', '1', 'vcolor',
           '--rename-interface-variable', 'in', '2', 'uv'],
}

# these dialects link varyings by name where every other backend links by location,
# so name both sides after the location
GL_VARYING_LOCATIONS = 8


def transpile_glsl(spirv_cross, spv_path, glsl_path, dialect, stage):
    """Transpile SPIR-V to OpenGL GLSL via spirv-cross. Returns True on success."""
    os.makedirs(os.path.dirname(glsl_path) or '.', exist_ok=True)
    cmd = [spirv_cross, spv_path, *GL_DIALECT_FLAGS[dialect], '--output', glsl_path]
    if stage == 'v':
        cmd += GL_VERTEX_INPUT_FLAGS[dialect]
    for location in range(GL_VARYING_LOCATIONS):
        cmd += ['--rename-interface-variable', 'out' if stage == 'v' else 'in', str(location), f'v{location}']
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f'spirv-cross (GLSL) failed for {spv_path}:', file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return False

    # --remap matches inputs by name, so one that isn't named like the canonical inputs stays a generic attribute,
    # which the legacy backend never feeds
    if dialect == 'legacy' and stage == 'v':
        with open(glsl_path, 'r') as f:
            leftover = re.findall(r'^attribute\s.*$', f.read(), re.MULTILINE)
        if leftover:
            print(f'{spv_path}: vertex inputs must be named inPos/inCol/inTex, found: {"; ".join(leftover)}',
                  file=sys.stderr)
            return False
    return True


def find_shaders(shader_dir):
    """Find all VK_*_{v,f}.glsl files. Returns list of (name, stage, path)."""
    pattern = os.path.join(shader_dir, 'VK_*_[vf].glsl')
    shaders = []
    for path in sorted(glob.glob(pattern)):
        basename = os.path.basename(path)
        m = re.match(r'^VK_(.+)_(v|f)\.glsl$', basename)
        if m:
            shaders.append((m.group(1), m.group(2), path))
    return shaders


def main():
    parser = argparse.ArgumentParser(description='Build all shader packs')
    parser.add_argument('--shader-dir', required=True, help='Directory containing VK_*.glsl files')
    parser.add_argument('--output-dir', required=True, help='Output directory for .spv and .shdpk files')
    parser.add_argument('--manifest', default=None,
                        help='Output manifest for the .shdpk packs (SDL_gpu); omit to skip .shdpk output')
    parser.add_argument('--dx11-manifest', dest='dx11_manifest', default=None,
                        help='Output manifest for the generated DX11 SM5.0 HLSL; omit to skip DX11 output')
    parser.add_argument('--dx11-output-dir', dest='dx11_output_dir', default=None,
                        help='Output directory for the generated DX11 .hlsl files (defaults to --output-dir)')
    parser.add_argument('--gl-manifest', dest='gl_manifest', default=None,
                        help='Output manifest for the generated OpenGL GLSL; omit to skip OpenGL output')
    parser.add_argument('--gl-dialect', dest='gl_dialect', choices=sorted(GL_DIALECT_FLAGS), default=None,
                        help='GLSL dialect of the enabled OpenGL backend (required with --gl-manifest)')
    parser.add_argument('--glslc', default=None, help='Path to glslc (optional; SPIR-V is skipped if not provided)')
    parser.add_argument('--dxc', default=None, help='Path to dxc (optional, for HLSL->DXIL)')
    parser.add_argument('--spirv-cross', dest='spirv_cross', default=None,
                        help='Path to spirv-cross (optional; transpiles SPIR-V to GLSL/HLSL/MSL)')
    args = parser.parse_args()

    if not args.manifest and not args.dx11_manifest and not args.gl_manifest:
        print('error: at least one of --manifest, --dx11-manifest or --gl-manifest is required', file=sys.stderr)
        sys.exit(1)

    # DX11 HLSL and OpenGL GLSL are transpiled from SPIR-V, so they need both glslc (SPIR-V) and spirv-cross
    if (args.dx11_manifest or args.gl_manifest) and not (args.glslc and args.spirv_cross):
        print('error: --dx11-manifest and --gl-manifest require both --glslc and --spirv-cross', file=sys.stderr)
        sys.exit(1)

    if args.gl_manifest and not args.gl_dialect:
        print('error: --gl-manifest requires --gl-dialect', file=sys.stderr)
        sys.exit(1)

    dx11_output_dir = args.dx11_output_dir or args.output_dir

    shaders = find_shaders(args.shader_dir)
    if not shaders:
        print(f'No VK_*_{{v,f}}.glsl files found in {args.shader_dir}', file=sys.stderr)
        sys.exit(1)

    shdpk_manifest_lines = ['# Auto-generated by pack_shaders.py - do not edit']
    dx11_manifest_lines = ['# Auto-generated by pack_shaders.py - do not edit']
    gl_manifest_lines = ['# Auto-generated by pack_shaders.py - do not edit']
    ok = True

    for name, stage, glsl_path in shaders:
        # compile GLSL -> SPIR-V once (the single source for every transpiled format)
        spv_path = None
        if args.glslc:
            spv_path = os.path.join(args.output_dir, f'VK_{name}_{stage}.spv')
            if not compile_glsl(args.glslc, glsl_path, spv_path, stage):
                ok = False
                continue

        # SDL_gpu .shdpk pack: canonical GLSL source + SPIR-V + (HLSL->DXIL) + MSL
        if args.manifest:
            # always include GLSL source (the canonical source; needed for runtime uniform block parsing)
            with open(glsl_path, 'rb') as f:
                sections = [(FMT_GLSL, f.read())]
            if spv_path:
                with open(spv_path, 'rb') as f:
                    sections.append((FMT_SPIRV, f.read()))

            # transpile SPIR-V -> HLSL (SM6.0) -> DXIL (needs spirv-cross + dxc, e.g. for D3D12)
            if spv_path and args.spirv_cross and args.dxc:
                hlsl_path = os.path.join(args.output_dir, f'VK_{name}_{stage}.hlsl')
                dxil_path = os.path.join(args.output_dir, f'VK_{name}_{stage}.dxil')
                if not transpile_hlsl(args.spirv_cross, spv_path, hlsl_path, '60') or \
                   not compile_hlsl(args.dxc, hlsl_path, dxil_path, stage):
                    ok = False
                    continue
                with open(dxil_path, 'rb') as f:
                    sections.append((FMT_DXIL, f.read()))

            # transpile SPIR-V -> MSL (needs spirv-cross; SDL3 compiles MSL at runtime, e.g. for Metal)
            if spv_path and args.spirv_cross:
                msl_path = os.path.join(args.output_dir, f'VK_{name}_{stage}.msl')
                if not transpile_msl(args.spirv_cross, spv_path, msl_path, stage):
                    ok = False
                    continue
                with open(msl_path, 'rb') as f:
                    sections.append((FMT_MSL, f.read()))

            # must have at least one binary/native format besides GLSL source
            if not any(fmt != FMT_GLSL for fmt, _ in sections):
                print(f'error: no binary shader format produced for VK_{name}_{stage} '
                      f'(need glslc for SPIR-V, plus spirv-cross for MSL/HLSL)', file=sys.stderr)
                ok = False
                continue

            shdpk_path = os.path.join(args.output_dir, f'VK_{name}_{stage}.shdpk')
            write_shdpk(shdpk_path, sections)
            shdpk_manifest_lines.append(f'VK_{name}_{stage}sh : {shdpk_path}')

        # DX11 backend: SM5.0 HLSL transpiled from the same SPIR-V, D3DCompile'd at runtime (no dxc)
        if args.dx11_manifest:
            dx11_hlsl_path = os.path.join(dx11_output_dir, f'DX11_{name}_{stage}.hlsl')
            if not transpile_hlsl(args.spirv_cross, spv_path, dx11_hlsl_path, '50'):
                ok = False
                continue
            dx11_manifest_lines.append(f'DX11_{name}_{stage}sh : {dx11_hlsl_path}')

        # OpenGL backends: GLSL transpiled from the same SPIR-V, compiled by the driver at runtime
        if args.gl_manifest:
            gl_glsl_path = os.path.join(args.output_dir, f'GL_{name}_{stage}.glsl')
            if not transpile_glsl(args.spirv_cross, spv_path, gl_glsl_path, args.gl_dialect, stage):
                ok = False
                continue
            gl_manifest_lines.append(f'GL_{name}_{stage}sh : {gl_glsl_path}')

    if not ok:
        sys.exit(1)

    if args.manifest:
        os.makedirs(os.path.dirname(args.manifest) or '.', exist_ok=True)
        with open(args.manifest, 'w') as f:
            f.write('\n'.join(shdpk_manifest_lines) + '\n')
    if args.dx11_manifest:
        os.makedirs(os.path.dirname(args.dx11_manifest) or '.', exist_ok=True)
        with open(args.dx11_manifest, 'w') as f:
            f.write('\n'.join(dx11_manifest_lines) + '\n')
    if args.gl_manifest:
        os.makedirs(os.path.dirname(args.gl_manifest) or '.', exist_ok=True)
        with open(args.gl_manifest, 'w') as f:
            f.write('\n'.join(gl_manifest_lines) + '\n')


if __name__ == '__main__':
    main()
