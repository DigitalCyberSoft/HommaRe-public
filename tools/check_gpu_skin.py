#!/usr/bin/env python3
"""Compile GPU skinning's shader code without a GPU or the game.

native/gpu_skin.cpp hands the renderer native/gpu_skin_math.h and its
d9_vfetch as text, compiled when the game first draws a skinned part: Metal
Shading Language on Apple systems, Vulkan GLSL elsewhere. This wraps both
fetches (the plain one and gpucheck's) the way the kit's shader generator does
(kit/dx/d3d9_shadergen.cpp) and compiles them offline:

    tools/check_gpu_skin.py                 # what this machine has
    tools/check_gpu_skin.py --msl --glsl    # fail if a compiler is missing

MSL with `xcrun metal` for macOS and iOS, GLSL with glslc for Vulkan 1.1
(SPIR-V 1.3, as host/gpu/vulkan/d3d9_vulkan.cpp asks glslang for).
"""

import argparse
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / "native"

MSL_MAIN = """
vertex float4 vs_main(uint vid [[vertex_id]], device const uint *vb0 [[buffer(18)]],
                      device const uint *vb1 [[buffer(19)]], device const uint *vb2 [[buffer(20)]],
                      constant uint *vb3 [[buffer(21)]]) {
    uint vw[8];
    if (d9_vfetch(vid, vb0, vb1, vb2, vb3, vw))
        return float4(as_type<float>(vw[0]), as_type<float>(vw[1]), as_type<float>(vw[2]), 1.0);
    return float4(0.0);
}
"""

GLSL_BUFFERS = "".join("layout(std430, set = 0, binding = %d) readonly buffer D9VB%d { uint d9_b%d[]; };\n"
                       % (4 + k, k, k) for k in range(4))

GLSL_MAIN = """
void main() {
    uint vw[8];
    if (d9_vfetch(uint(gl_VertexIndex), vw))
        gl_Position = vec4(uintBitsToFloat(vw[0]), uintBitsToFloat(vw[1]), uintBitsToFloat(vw[2]), 1.0);
    else
        gl_Position = vec4(0.0);
}
"""


def fetches(source, lang):
    """name -> body of gpu_skin.cpp's raw strings in `lang` (MSL or GLSL)."""
    found = dict(re.findall(r'static const char (k\w+)\[\] = R"%s\((.*?)\)%s";' % (lang, lang), source, re.S))
    if len(found) != 2:
        sys.exit("native/gpu_skin.cpp: expected two %s fetches, found %s" % (lang, sorted(found)))
    return found


def compile_all(units, command, suffix, out):
    """Compile each (name, text) with command(path); the failures' names."""
    failed = []
    for name, text in units:
        path = out / (name + suffix)
        path.write_text(text)
        result = subprocess.run(command(path), capture_output=True, text=True)
        status = "ok" if result.returncode == 0 else "FAILED"
        print("%-28s %s" % (path.name, status), flush=True)
        if result.returncode:
            sys.stdout.write(result.stdout + result.stderr)
            failed.append(path.name)
    return failed


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--msl", action="store_true", help="require the Metal compiler (xcrun metal)")
    p.add_argument("--glsl", action="store_true", help="require glslc")
    args = p.parse_args()
    math = (NATIVE / "gpu_skin_math.h").read_text()
    source = (NATIVE / "gpu_skin.cpp").read_text()
    metal = platform.system() == "Darwin" and subprocess.run(
        ["xcrun", "-f", "metal"], capture_output=True).returncode == 0
    glslc = shutil.which("glslc")
    if args.msl and not metal:
        sys.exit("--msl: no Metal compiler (xcrun -f metal)")
    if args.glsl and not glslc:
        sys.exit("--glsl: no glslc on PATH")
    if not metal and not glslc:
        sys.exit("Neither xcrun metal nor glslc is here: nothing to compile with")
    failed = []
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        if metal:
            units = [(name, "#include <metal_stdlib>\nusing namespace metal;\n" + math + body + MSL_MAIN)
                     for name, body in fetches(source, "MSL").items()]
            for sdk in ("macosx", "iphoneos"):
                failed += compile_all([(n + "-" + sdk, t) for n, t in units],
                                      lambda path, sdk=sdk: ["xcrun", "-sdk", sdk, "metal", "-c", str(path),
                                                             "-o", str(path.with_suffix(".air"))],
                                      ".metal", out)
        if glslc:
            units = [(name, "#version 450\n" + GLSL_BUFFERS + math + body + GLSL_MAIN)
                     for name, body in fetches(source, "GLSL").items()]
            failed += compile_all(units, lambda path: [glslc, "-fshader-stage=vert", "--target-env=vulkan1.1",
                                                       "--target-spv=spv1.3", str(path),
                                                       "-o", str(path.with_suffix(".spv"))],
                                  ".vert", out)
    if failed:
        sys.exit("%d of the GPU skinning shaders failed to compile" % len(failed))
    print("GPU skinning's shader code compiles")


if __name__ == "__main__":
    main()
