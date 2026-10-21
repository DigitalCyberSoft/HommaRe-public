// d3d9_msl.h - Direct3D 9 shaders as Metal Shading Language source.
//
// A decoded program (d3d9_shader.h) becomes one MSL function, `vs_main` or
// `ps_main`, in a source string of its own. Every vertex shader writes the same
// interpolant struct and every pixel shader reads it, so any vertex shader
// links with any pixel shader, as in Direct3D.
//
// Resource slots the generated functions use:
//
//   vertex   [[stage_in]] attribute(i)  the dcl'd input register v<i>, as float4
//            buffer(16)                 float4 constants c0..
//            buffer(17)                 D9VSParams
//            buffer(18..21)             vertex fetch only (VFetchLayout): the game's buffers b0..b3
//            buffer(22), buffer(23)     vertex fetch's check: what it fetched, and the frame tag
//   fragment buffer(0)                  float4 constants c0..
//            buffer(1)                  D9PSParams
//            texture(i), sampler(i)     sampler stage i
//
// The host fills both parameter blocks with the layouts below; they are part
// of the generated source's contract.
#pragma once
#include "d3d9_shader.h"

#include <cstdint>
#include <string>

struct D9VSParams {
    float halfpix[4];     // added to xy, times w: D3D's half-pixel offset in clip space
    float ascale[16][4];  // per input register: undoes Metal's normalization of integer formats
    int32_t bconst[4][4]; // packed Boolean registers, matching uniform-buffer alignment
};
struct D9PSParams {
    float alpha_ref;    // 0..1
    int32_t alpha_func; // D3DCMPFUNC; 8 (always) means no test
    int32_t fog_mode;   // 0 off, 1 vertex factor, 2 linear, 3 exp, 4 exp2
    int32_t pad0;
    float fog_color[4];
    float fog_start, fog_end, fog_density, pad1;
    int32_t bconst[4][4];
};

namespace d9msl {

// What a pixel shader cannot say about its own samplers: for shader model
// 1.x the texture kinds and projection, for every model the stages bound to a
// depth texture. Direct3D 9 drivers sample a depth texture as a shadow map:
// the lookup compares the coordinate's z with the stored depth (LESSEQUAL,
// filtered) and returns the result in r, g and b.
struct PixelVariant {
    uint16_t cube_mask = 0;      // stages whose bound texture is a cube map
    uint16_t projected_mask = 0; // stages with D3DTTFF_PROJECTED
    uint16_t depth_mask = 0;     // stages whose bound texture holds depth
    uint16_t alpha_mask = 0;     // stages stored as red that sample as alpha (WGSL only)
    uint64_t key() const {
        return (uint64_t)alpha_mask << 48 | (uint64_t)depth_mask << 32 | (uint64_t)cube_mask << 16 |
               projected_mask;
    }
};

// Vertex fetch: for the input registers the vertex declaration feeds from
// stream 0, the element's D3DDECLTYPE and byte offset. A vertex shader made
// with one runs the game's `d9_vfetch` (host_d9_vfetch_prelude) for its
// vertex number first and, when that supplies the vertex, decodes those
// registers from the words it wrote instead of from the vertex buffer.
struct VFetchLayout {
    uint8_t type[16] = {};
    uint8_t offset[16] = {};
    uint16_t mask = 0;   // registers fed from stream 0
    uint8_t words = 8;   // the vertex's size in words (the stream's stride / 4)
    bool check = false;  // also write what it fetched (buffer 22), tagged (buffer 23)
    uint64_t key() const {
        uint64_t h = 1469598103934665603ull ^ mask ^ (uint64_t)words << 16 ^ (uint64_t)check << 24;
        for (int i = 0; i < 16; ++i)
            h = (h ^ ((uint64_t)type[i] << 8 | offset[i])) * 1099511628211ull;
        return h | 1;
    }
};
// The types VFetchLayout can decode.
bool vfetch_type_ok(uint32_t d3ddecltype);

// False, with the reason, for a program the translator does not handle.
bool vertex_source(const d9sh::Program &p, std::string *out, std::string *why);
// The same with the vertex fetch prefix; prelude is the game's MSL.
bool vertex_source(const d9sh::Program &p, const VFetchLayout &vf, const char *prelude,
                   std::string *out, std::string *why);
bool pixel_source(const d9sh::Program &p, const PixelVariant &v, std::string *out,
                  std::string *why);

// The stages a pixel shader samples, for the host's texture binding.
uint32_t pixel_sampler_mask(const d9sh::Program &p);
// Which colour outputs a 2.0 pixel shader writes (bit i: oC<i>).
uint32_t pixel_color_outputs(const d9sh::Program &p);

} // namespace d9msl
