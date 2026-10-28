/* gpu_skin_math.h - one skinned, lit vertex of the game's format 5 (0x20 bytes:
 * position, normal, two uv pairs, two light colours in TANGENT0/1), computed
 * from the bind pose the way the game's CPU path computes it, so the two can
 * be compared byte for byte. Compiled three times: as C++ into native/gpu_skin.cpp
 * (the reference and check mode), and as Metal Shading Language or Vulkan
 * GLSL, embedded as text, in the vertex shaders' fetch (kit d3d9_vfetch).
 * GLSL has no pointers: there a pointer into POOL, HEAP or TAB is a word
 * index into the fetch's buffer d9_b1, d9_b2 or d9_b3, read through
 * H5_POOL_AT / H5_HEAP_AT / H5_TAB_AT, and a thread array is an array
 * parameter (H5_IN, H5_OUT). It has no implicit bool either, hence the
 * explicit comparisons with zero.
 *
 * The game's functions it follows (Tribes of the East addresses; the base
 * game's in brackets, design notes gpu-skinning-plan.md has the formulas):
 *   positions  005578e0 / 00557f40 [0066f600 / 0066fc60] - float, by bone;
 *   normals    00557470 / 00557220 / 00557330 [0066f2e0 / f090 / f1a0] -
 *              integer MMX through the bones' i16 matrices, renormalised by
 *              the reciprocal square root table;
 *   light      005866e0 [0068ed90] directional N.L, 00587510 [0068fbc0] the
 *              mesh's colour channel 0xd, 00587100 [0068f7b0] point lights
 *              (one vector per part on the bind-pose normal), 00586180
 *              [0068e830] the sum in "gamma 3" with the cube-root table;
 *   vertex     005856c0 [0068dd70] the writer.
 *
 * Buffers, in 32-bit words:
 *   POOL  16 words a mesh vertex: bind position (3 floats), the record's
 *         normal, uv0, uv1, baked colours (2), position weights (4 floats),
 *         the 4 normal weight bytes, the 4 bone bytes, colour channel 0xd, 0.
 *   HEAP  part records (H5_PART_*) and the bones' palettes: per bone 12
 *         floats (3x4 rows) at the record's float palette, 6 words (12 i16,
 *         CMMXAnimationMatrices' layout) at its i16 palette.
 *   TAB   the reciprocal square root table (16384 u16, the game's 01139088)
 *         then the cube-root table (32768 u8, the game's 01142088).
 * A MAP word (the vertex buffer's vertex -> what to draw) is part record
 * index << 16 | the vertex's index in its mesh. */
#ifndef GPU_SKIN_MATH_H
#define GPU_SKIN_MATH_H

#ifdef __METAL_VERSION__
#define H5_PTR device const uint *
#define H5_TPTR constant uint *
#define H5_IN(t, name, n) thread t *name
#define H5_OUT(t, name, n) thread t *name
#define H5_POOL_AT(p, i) (p)[i]
#define H5_HEAP_AT(p, i) (p)[i]
#define H5_TAB_AT(p, i) (p)[i]
#define H5_I32(x) ((int)(x))
#define H5_U32(x) ((uint)(x))
#define H5_PRECISE
#define H5_FN static inline
typedef int h5_i32;
typedef uint h5_u32;
static inline float h5_f(h5_u32 w) { return as_type<float>(w); }
static inline h5_u32 h5_w(float f) { return as_type<h5_u32>(f); }
#elif defined(VULKAN)
#define H5_PTR uint
#define H5_TPTR uint
#define H5_IN(t, name, n) t name[n]
#define H5_OUT(t, name, n) out t name[n]
#define H5_POOL_AT(p, i) d9_b1[(p) + (i)]
#define H5_HEAP_AT(p, i) d9_b2[(p) + (i)]
#define H5_TAB_AT(p, i) d9_b3[(p) + (i)]
#define H5_I32(x) int(x)
#define H5_U32(x) uint(x)
/* The positions as the CPU rounds them: no fused multiply-adds. */
#define H5_PRECISE precise
#define H5_FN
#define h5_i32 int
#define h5_u32 uint
float h5_f(h5_u32 w) { return uintBitsToFloat(w); }
h5_u32 h5_w(float f) { return floatBitsToUint(f); }
#else
#include <stdint.h>
#include <string.h>
#define H5_PTR const h5_u32 *
#define H5_TPTR const h5_u32 *
#define H5_IN(t, name, n) t *name
#define H5_OUT(t, name, n) t *name
#define H5_POOL_AT(p, i) (p)[i]
#define H5_HEAP_AT(p, i) (p)[i]
#define H5_TAB_AT(p, i) (p)[i]
#define H5_I32(x) ((h5_i32)(x))
#define H5_U32(x) ((h5_u32)(x))
#define H5_PRECISE
#define H5_FN static inline
typedef int32_t h5_i32;
typedef uint32_t h5_u32;
static inline float h5_f(h5_u32 w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}
static inline h5_u32 h5_w(float f) {
    h5_u32 w;
    memcpy(&w, &f, 4);
    return w;
}
#endif

/* Part record layout (words). */
#define H5_PART_POOL 0      /* the mesh's first vertex in POOL */
#define H5_PART_PALF 1      /* float palette, word offset in HEAP */
#define H5_PART_PALI 2      /* i16 palette, word offset in HEAP */
#define H5_PART_BONES 3     /* bones in the palettes */
#define H5_PART_FLAGS 4
#define H5_PART_ALPHA 5     /* TANGENT0's alpha | TANGENT1's << 8 */
#define H5_PART_NLIGHT 6    /* static lights | dynamic << 8 */
#define H5_PART_BASE 8      /* lightctx+0x00: base, 4 i16 */
#define H5_PART_C0 10       /* +0x08 */
#define H5_PART_C1 12       /* +0x10 */
#define H5_PART_BACK 14     /* +0x18 */
#define H5_PART_SEC 16      /* +0x18, or C0 x tint (the part's +0x2a) */
#define H5_PART_DIR 18      /* +0x20 */
#define H5_PART_BIAS 20     /* +0x28 */
#define H5_PART_LIGHTS 24   /* 4 words a light: vector (4 i16), colour (4 i16) */
#define H5_PART_MAXLIGHTS 16
#define H5_PART_WORDS 96

#define H5_FLAG_BAKED 1u    /* the part's +0x26: baked colours */
#define H5_FLAG_GRAY 2u     /* +0x27 with +0x26: 0x404040 */
#define H5_FLAG_CH13 4u     /* the mesh has colour channel 0xd */
#define H5_FLAG_STATIC 8u   /* a static light passed the box test */
#define H5_FLAG_DYN 16u     /* a dynamic one did */

#define H5_TAB_RSQRT 0      /* word offset of the rsqrt table in TAB */
#define H5_TAB_CBRT 8192    /* word offset of the cube-root table */

H5_FN h5_i32 h5_s16(h5_i32 x) { return ((x & 0xffff) ^ 0x8000) - 0x8000; }
H5_FN h5_i32 h5_sat16(h5_i32 x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : x; }
H5_FN h5_i32 h5_mulhi(h5_i32 a, h5_i32 b) { return (a * b) >> 16; }
/* PSLLW: a 16-bit shift left, wrapping. */
H5_FN h5_i32 h5_shl16(h5_i32 x, h5_u32 n) { return h5_s16(H5_I32((H5_U32(x) & 0xffffu) << n)); }
H5_FN h5_u32 h5_satu8(h5_i32 x) { return x < 0 ? 0u : x > 255 ? 255u : H5_U32(x); }
/* Word k of a packed 4 x i16 vector stored as two words (in HEAP). */
H5_FN h5_i32 h5_word(H5_PTR v, h5_u32 k) {
    return h5_s16(H5_I32(H5_HEAP_AT(v, k >> 1) >> ((k & 1u) * 16u)));
}
H5_FN h5_u32 h5_tab16(H5_TPTR t, h5_u32 i) {
    return (H5_TAB_AT(t, H5_TAB_RSQRT + (i >> 1)) >> ((i & 1u) * 16u)) & 0xffffu;
}
H5_FN h5_u32 h5_tab8(H5_TPTR t, h5_u32 i) {
    return (H5_TAB_AT(t, H5_TAB_CBRT + (i >> 2)) >> ((i & 3u) * 8u)) & 0xffu;
}

/* One bone's R(M): the normal's words through the rotated rows, saturating
 * (00557470's PMULHW / PADDSW sequence). n[3] is zero. */
H5_FN void h5_rotate(H5_IN(h5_i32, n, 4), H5_PTR m, H5_OUT(h5_i32, r, 4)) {
    const h5_i32 a1[4] = {n[2], h5_s16(n[0] + n[3]), n[1], n[2]};
    const h5_i32 a2[4] = {n[1], n[2], h5_s16(n[0] + n[3]), n[1]};
    for (h5_u32 k = 0u; k < 4u; ++k) {
        const h5_i32 s = h5_sat16(h5_mulhi(n[k], h5_word(m, k)) + h5_mulhi(a1[k], h5_word(m, 4u + k)));
        r[k] = h5_sat16(s + h5_mulhi(a2[k], h5_word(m + 4u, k)));
    }
}

/* The skinned normal: rec is the mesh record's normal, wb the four weight
 * bytes, bone the four bone bytes, pali the i16 palette (6 words a bone). */
H5_FN h5_u32 h5_normal(h5_u32 rec, h5_u32 wb, h5_u32 bone, H5_PTR pali, h5_u32 nbones, H5_TPTR tab) {
    h5_i32 n[4];
    for (h5_u32 k = 0u; k < 3u; ++k)
        n[k] = h5_s16(H5_I32(((rec >> (8u * k)) & 0xffu) << 8) - 0x8000);
    n[3] = 0;
    const h5_u32 w1 = (wb >> 8) & 0xffu, w2 = (wb >> 16) & 0xffu;
    const h5_u32 last = nbones != 0u ? nbones - 1u : 0u;
    h5_i32 w[4];
    if (w1 == 0u) {
        h5_u32 b = bone & 0xffu;
        b = b < nbones ? b : last;
        h5_i32 r[4];
        h5_rotate(n, pali + b * 6u, r);
        for (h5_u32 k = 0u; k < 4u; ++k)
            w[k] = h5_shl16(r[k], 3u);
    } else {
        const h5_u32 nb = w2 != 0u ? 3u : 2u;
        h5_i32 sum[4] = {0, 0, 0, 0};
        for (h5_u32 j = 0u; j < nb; ++j) {
            h5_u32 b = (bone >> (8u * j)) & 0xffu;
            b = b < nbones ? b : last;
            const h5_i32 wt = H5_I32(((wb >> (8u * j)) & 0xffu) << 6);
            h5_i32 r[4];
            h5_rotate(n, pali + b * 6u, r);
            for (h5_u32 k = 0u; k < 4u; ++k) {
                const h5_i32 p = h5_mulhi(h5_shl16(r[k], 4u), wt);
                sum[k] = j != 0u ? h5_sat16(sum[k] + p) : p;
            }
        }
        for (h5_u32 k = 0u; k < 4u; ++k)
            w[k] = h5_shl16(sum[k], 3u);
    }
    const h5_u32 len2 = H5_U32(w[0] * w[0] + w[1] * w[1]) + H5_U32(w[2] * w[2] + w[3] * w[3]);
    const h5_i32 rs = H5_I32(h5_tab16(tab, len2 >> 18));
    h5_u32 o = rec & 0xff000000u;
    for (h5_u32 k = 0u; k < 4u; ++k) {
        const h5_u32 bias = k < 3u ? 0x8080u : 0u;
        const h5_u32 v = ((H5_U32(h5_shl16(h5_mulhi(w[k], rs), 5u)) + bias) & 0xffffu) >> 8;
        o |= v << (8u * k);
    }
    return o;
}

/* N.D of a packed normal with the decode bias, as PMADDWD + PADDD + PSRAD 15,
 * then the low word. */
H5_FN h5_i32 h5_ndot(h5_u32 nrm, H5_PTR bias, H5_PTR dir) {
    h5_i32 d = 0;
    for (h5_u32 k = 0u; k < 4u; ++k) {
        const h5_i32 nk = h5_s16(H5_I32(((nrm >> (8u * k)) & 0xffu) * 257u) - h5_word(bias, k));
        d += nk * h5_word(dir, k);
    }
    return d >> 15;
}

/* 005866e0: the two directional colours of one normal, o[0] and o[1]. */
H5_FN void h5_directional(h5_u32 nrm, H5_PTR part, H5_OUT(h5_u32, o, 2)) {
    const h5_i32 s = h5_s16(h5_ndot(nrm, part + H5_PART_BIAS, part + H5_PART_DIR));
    const h5_i32 pos = s >= 0 ? s : 0;
    const h5_i32 neg = s < 0 ? h5_s16(~s) : -1;
    h5_u32 a = 0u, b = 0u;
    for (h5_u32 k = 0u; k < 4u; ++k) {
        const h5_i32 base = h5_word(part + H5_PART_BASE, k);
        const h5_i32 x0 = h5_s16(h5_s16(base + h5_mulhi(h5_word(part + H5_PART_C0, k), pos)) +
                                 h5_mulhi(h5_word(part + H5_PART_SEC, k), neg));
        const h5_i32 x1 = h5_s16(h5_s16(base + h5_mulhi(h5_word(part + H5_PART_C1, k), pos)) +
                                 h5_mulhi(h5_word(part + H5_PART_BACK, k), neg));
        a |= h5_satu8(x0 >> 4) << (8u * k);
        b |= h5_satu8(x1 >> 4) << (8u * k);
    }
    o[0] = a;
    o[1] = b;
}

/* 00587510: per byte (c * m) >> 8. */
H5_FN h5_u32 h5_modulate(h5_u32 c, h5_u32 m) {
    h5_u32 o = 0u;
    for (h5_u32 k = 0u; k < 4u; ++k)
        o |= ((((c >> (8u * k)) & 0xffu) * ((m >> (8u * k)) & 0xffu)) >> 8) << (8u * k);
    return o;
}

/* 00587100: the lights [first, first + count) of the part added up on the
 * mesh record's normal (words 0..2; the fourth never reaches the vertex). */
H5_FN void h5_point(h5_u32 nrm, H5_PTR part, h5_u32 first, h5_u32 count, H5_OUT(h5_i32, acc, 3)) {
    acc[0] = acc[1] = acc[2] = 0;
    for (h5_u32 j = 0u; j < count; ++j) {
        H5_PTR l = part + H5_PART_LIGHTS + (first + j) * 4u;
        h5_i32 d = 0;
        for (h5_u32 k = 0u; k < 3u; ++k)
            d += h5_s16(H5_I32(((nrm >> (8u * k)) & 0xffu) * 257u) - 0x8000) * h5_word(l, k);
        d = h5_sat16(d >> 15);
        d = d > 0 ? d : 0;
        for (h5_u32 k = 0u; k < 3u; ++k)
            acc[k] = h5_sat16(acc[k] + h5_sat16((d * h5_word(l + 2u, k) + 0x1000) >> 13));
    }
}

/* 00586180: a colour lifted to "gamma 3", the light added, back through the
 * cube-root table. */
H5_FN h5_u32 h5_combine(h5_u32 c, H5_IN(h5_i32, acc, 3), H5_TPTR tab) {
    h5_u32 o = 0u;
    for (h5_u32 k = 0u; k < 3u; ++k) {
        const h5_i32 x = H5_I32((((c >> (8u * k)) & 0xffu) * 257u) >> 1);
        const h5_i32 x2 = h5_shl16(h5_mulhi(x, x), 1u);
        const h5_i32 x3 = h5_shl16(h5_mulhi(x2, x), 1u);
        const h5_u32 lo = ((H5_U32(x2 * x) & 0xffffu) >> 2) | 0x4000u;
        const h5_u32 y = (H5_U32(h5_sat16(x3 + acc[k])) & 0xffffu) >> 1;
        const h5_u32 idx = y != 0u ? y : lo;
        o |= h5_tab8(tab, idx & 0x7fffu) << (8u * k);
    }
    return o;
}

/* Bone b's 3x4 float rows (in HEAP) applied to p, in the game's order of terms. */
H5_FN float h5_row(H5_PTR m, h5_u32 i) { return h5_f(H5_HEAP_AT(m, i)); }
H5_FN void h5_bone(H5_PTR m, float x, float y, float z, H5_OUT(float, o, 3)) {
    H5_PRECISE float o0 = y * h5_row(m, 1u) + x * h5_row(m, 0u) + z * h5_row(m, 2u) + h5_row(m, 3u);
    H5_PRECISE float o1 = x * h5_row(m, 4u) + y * h5_row(m, 5u) + z * h5_row(m, 6u) + h5_row(m, 7u);
    H5_PRECISE float o2 = x * h5_row(m, 8u) + y * h5_row(m, 9u) + z * h5_row(m, 10u) + h5_row(m, 11u);
    o[0] = o0;
    o[1] = o1;
    o[2] = o2;
}

/* The vertex MAP word m names, as the 8 words of a format 5 vertex. */
H5_FN void h5_skin_vertex(h5_u32 m, H5_PTR pool, H5_PTR heap, H5_TPTR tab, H5_OUT(h5_u32, w, 8)) {
    H5_PTR part = heap + (m >> 16) * H5_PART_WORDS;
    H5_PTR v = pool + (H5_HEAP_AT(part, H5_PART_POOL) + (m & 0xffffu)) * 16u;
    const h5_u32 nbones = H5_HEAP_AT(part, H5_PART_BONES);
    const h5_u32 last = nbones != 0u ? nbones - 1u : 0u;
    H5_PTR palf = heap + H5_HEAP_AT(part, H5_PART_PALF);
    const float x = h5_f(H5_POOL_AT(v, 0u)), y = h5_f(H5_POOL_AT(v, 1u)), z = h5_f(H5_POOL_AT(v, 2u));
    const h5_u32 wb = H5_POOL_AT(v, 12u), bone = H5_POOL_AT(v, 13u);
    H5_PRECISE float p[3];
    if (((wb >> 8) & 0xffu) == 0u) {
        h5_u32 b = bone & 0xffu;
        b = b < nbones ? b : last;
        h5_bone(palf + b * 12u, x, y, z, p);
    } else {
        p[0] = p[1] = p[2] = 0.0f;
        for (h5_u32 j = 0u; j < 4u && ((wb >> (8u * j)) & 0xffu) != 0u; ++j) {
            h5_u32 b = (bone >> (8u * j)) & 0xffu;
            b = b < nbones ? b : last;
            float q[3];
            h5_bone(palf + b * 12u, x, y, z, q);
            const float wt = h5_f(H5_POOL_AT(v, 8u + j));
            p[0] = q[0] * wt + p[0];
            p[1] = q[1] * wt + p[1];
            p[2] = q[2] * wt + p[2];
        }
    }
    const h5_u32 flags = H5_HEAP_AT(part, H5_PART_FLAGS);
    const h5_u32 rec = H5_POOL_AT(v, 3u);
    const h5_u32 nrm = h5_normal(rec, wb, bone, heap + H5_HEAP_AT(part, H5_PART_PALI), nbones, tab);
    h5_u32 o[2];
    if ((flags & H5_FLAG_BAKED) != 0u) {
        o[0] = (flags & H5_FLAG_GRAY) != 0u ? 0x404040u : H5_POOL_AT(v, 7u);
        o[1] = (flags & H5_FLAG_GRAY) != 0u ? 0x404040u : H5_POOL_AT(v, 6u);
    } else {
        h5_directional(nrm, part, o);
        if ((flags & H5_FLAG_CH13) != 0u) {
            o[1] = h5_modulate(o[1], H5_POOL_AT(v, 14u));
            o[0] = h5_modulate(o[0], H5_POOL_AT(v, 14u));
        }
        const h5_u32 nl = H5_HEAP_AT(part, H5_PART_NLIGHT);
        const h5_u32 ns = nl & 0xffu, nd = (nl >> 8) & 0xffu;
        h5_i32 acc[3];
        if ((flags & H5_FLAG_STATIC) != 0u) {
            h5_point(rec, part, 0u, ns, acc);
            o[0] = h5_combine(o[0], acc, tab);
            o[1] = h5_combine(o[1], acc, tab);
        }
        if ((flags & H5_FLAG_DYN) != 0u) {
            h5_point(rec, part, ns, nd, acc);
            o[0] = h5_combine(o[0], acc, tab);
            o[1] = h5_combine(o[1], acc, tab);
        }
    }
    const h5_u32 alpha = H5_HEAP_AT(part, H5_PART_ALPHA);
    w[0] = h5_w(p[0]);
    w[1] = h5_w(p[1]);
    w[2] = h5_w(p[2]);
    w[3] = nrm;
    w[4] = H5_POOL_AT(v, 4u);
    w[5] = H5_POOL_AT(v, 5u);
    w[6] = (alpha & 0xffu) << 24 | (o[1] & 0xffffffu);
    w[7] = ((alpha >> 8) & 0xffu) << 24 | (o[0] & 0xffffffu);
}

#endif
