// d3d9_glsl.h - Direct3D 9 shaders as Vulkan GLSL (4.50) source.
//
// The same translation as d3d9_msl.h, for the Vulkan renderer: a decoded
// program becomes one `main` in a source string of its own, compiled to
// SPIR-V by the renderer. Every vertex shader writes the same interpolants
// (locations 0-11) and every pixel shader reads them, so any vertex shader
// links with any pixel shader.
//
// Descriptor set 0, the same layout for every program:
//
//   binding 0   uniform VSC { vec4 c[N]; }        vertex float constants
//   binding 1   uniform VSP (D9VkVSParams)
//   binding 2   uniform PSC { vec4 c[N]; }        pixel float constants
//   binding 3   uniform PSP (D9PSParams, d3d9_msl.h)
//   binding 4+i combined image sampler, stage i (0-15): sampler2D,
//               samplerCube, or sampler2DShadow for a depth texture
//   binding 20+k storage buffer, vertex fetch only (VFetchLayout): the
//               game's buffers d9_b0..d9_b3, then (24) the check's d9_chk
//
// N is constant_registers(program): each program declares only what it
// reads, so the renderer binds N * 16 bytes of constants.
//
// Vertex input register v<i> is location i, as vec4.
#pragma once
#include "d3d9_msl.h"

#include <cstdint>
#include <string>

struct D9VkVSParams {
    float halfpix[4];    // added to xy, times w: D3D's half-pixel offset in clip space
    float ascale[16][4]; // per input register: undoes normalization of integer formats
    int32_t bgra[4];     // [0]: input registers stored as RGBA whose D3D order is BGRA
    int32_t bconst[4][4];
    uint32_t vtag[4]; // [0]: the vertex fetch check's tag (HostD9Draw::vfetch_tag)
};

namespace d9glsl {

constexpr uint32_t kConstantRegisters = 256;
constexpr uint32_t kSamplerBinding = 4;
constexpr uint32_t kVFetchBinding = 20;

bool vertex_source(const d9sh::Program &p, std::string *out, std::string *why);
// The same with the vertex fetch prefix (d3d9_msl.h); prelude is the game's GLSL.
bool vertex_source(const d9sh::Program &p, const d9msl::VFetchLayout &vf, const char *prelude,
                   std::string *out, std::string *why);
bool pixel_source(const d9sh::Program &p, const d9msl::PixelVariant &v, std::string *out,
                  std::string *why);
// The float constant registers the program's source declares.
uint32_t constant_registers(const d9sh::Program &p);

} // namespace d9glsl
