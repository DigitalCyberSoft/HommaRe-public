/* neon_compat.h - the NEON intrinsics the native code uses (gpu_skin.cpp,
 * lighting.c, scene.c), for the targets without them: clang's vector
 * extensions, lane for lane what NEON does. The integer arithmetic wraps
 * (done on unsigned lanes) or saturates exactly as NEON's and the game's MMX
 * do; each float operation is a single IEEE one, never fused, so the results
 * are bit for bit those of arm64 and of the game's SSE. Their check modes
 * compare them with the translated originals all the same. */
#pragma once
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#else
#include <stdint.h>
#include <string.h>

typedef uint8_t uint8x8_t __attribute__((vector_size(8)));
typedef uint8_t uint8x16_t __attribute__((vector_size(16)));
typedef int16_t int16x4_t __attribute__((vector_size(8)));
typedef uint16_t uint16x4_t __attribute__((vector_size(8)));
typedef int16_t int16x8_t __attribute__((vector_size(16)));
typedef uint16_t uint16x8_t __attribute__((vector_size(16)));
typedef int32_t int32x2_t __attribute__((vector_size(8)));
typedef int32_t int32x4_t __attribute__((vector_size(16)));
typedef uint32_t uint32x2_t __attribute__((vector_size(8)));
typedef uint32_t uint32x4_t __attribute__((vector_size(16)));
typedef uint64_t uint64x1_t __attribute__((vector_size(8)));
typedef float float32x4_t __attribute__((vector_size(16)));
typedef struct {
    float32x4_t val[4];
} float32x4x4_t;

#define NC_INLINE static inline __attribute__((always_inline))

/* Same-size reinterpretations: a vector cast is a bitcast. */
NC_INLINE uint64_t vget_lane_u64(uint64x1_t v, int lane) { return v[lane]; }
NC_INLINE uint64x1_t vreinterpret_u64_s16(int16x4_t v) { return (uint64x1_t)v; }
NC_INLINE uint64x1_t vreinterpret_u64_s32(int32x2_t v) { return (uint64x1_t)v; }
NC_INLINE uint64x1_t vreinterpret_u64_u8(uint8x8_t v) { return (uint64x1_t)v; }
NC_INLINE int16x4_t vreinterpret_s16_u64(uint64x1_t v) { return (int16x4_t)v; }
NC_INLINE int16x4_t vreinterpret_s16_u16(uint16x4_t v) { return (int16x4_t)v; }
NC_INLINE uint16x4_t vreinterpret_u16_s16(int16x4_t v) { return (uint16x4_t)v; }
NC_INLINE uint8x16_t vreinterpretq_u8_u32(uint32x4_t v) { return (uint8x16_t)v; }
NC_INLINE uint8x8_t vcreate_u8(uint64_t v) { return (uint8x8_t)(uint64x1_t){v}; }
NC_INLINE uint64x1_t vcreate_u64(uint64_t v) { return (uint64x1_t){v}; }

/* Lanes split, joined, widened, narrowed. */
NC_INLINE uint16x4_t vget_low_u16(uint16x8_t v) { return __builtin_shufflevector(v, v, 0, 1, 2, 3); }
NC_INLINE uint16x4_t vget_high_u16(uint16x8_t v) { return __builtin_shufflevector(v, v, 4, 5, 6, 7); }
NC_INLINE int32x2_t vget_low_s32(int32x4_t v) { return __builtin_shufflevector(v, v, 0, 1); }
NC_INLINE int32x2_t vget_high_s32(int32x4_t v) { return __builtin_shufflevector(v, v, 2, 3); }
NC_INLINE int16x8_t vcombine_s16(int16x4_t a, int16x4_t b) {
    return __builtin_shufflevector(a, b, 0, 1, 2, 3, 4, 5, 6, 7);
}
NC_INLINE uint16x8_t vmovl_u8(uint8x8_t v) { return __builtin_convertvector(v, uint16x8_t); }
NC_INLINE int16x4_t vdup_n_s16(int16_t x) { return (int16x4_t){x, x, x, x}; }
NC_INLINE uint32x4_t vdupq_n_u32(uint32_t x) { return (uint32x4_t){x, x, x, x}; }

/* Integer arithmetic: wrapping on unsigned lanes, saturating where NEON's does. */
NC_INLINE int16x4_t vadd_s16(int16x4_t a, int16x4_t b) { return (int16x4_t)((uint16x4_t)a + (uint16x4_t)b); }
NC_INLINE int16x4_t vsub_s16(int16x4_t a, int16x4_t b) { return (int16x4_t)((uint16x4_t)a - (uint16x4_t)b); }
NC_INLINE int16x4_t vqadd_s16(int16x4_t a, int16x4_t b) { return __builtin_elementwise_add_sat(a, b); }
#define vshl_n_u16(v, n) ((uint16x4_t)(v) << (n))
#define vshl_n_s16(v, n) ((int16x4_t)((uint16x4_t)(v) << (n)))
#define vshr_n_u16(v, n) ((uint16x4_t)(v) >> (n))
NC_INLINE int32x4_t vmull_s16(int16x4_t a, int16x4_t b) {
    /* |a*b| <= 2^30: no lane overflows */
    return __builtin_convertvector(a, int32x4_t) * __builtin_convertvector(b, int32x4_t);
}
#define vshrn_n_s32(v, n) __builtin_convertvector((int32x4_t)(v) >> (n), int16x4_t)
NC_INLINE int32x2_t vpadd_s32(int32x2_t a, int32x2_t b) {
    const uint32x2_t lo = __builtin_shufflevector((uint32x2_t)a, (uint32x2_t)b, 0, 2);
    const uint32x2_t hi = __builtin_shufflevector((uint32x2_t)a, (uint32x2_t)b, 1, 3);
    return (int32x2_t)(lo + hi);
}
NC_INLINE uint32x4_t vaddw_u16(uint32x4_t a, uint16x4_t b) { return a + __builtin_convertvector(b, uint32x4_t); }
NC_INLINE uint8x8_t vqmovun_s16(int16x8_t v) {
    const int16x8_t zero = {0}, top = {255, 255, 255, 255, 255, 255, 255, 255};
    return __builtin_convertvector(__builtin_elementwise_min(__builtin_elementwise_max(v, zero), top), uint8x8_t);
}

/* Floats: one IEEE operation each. */
NC_INLINE float32x4_t vaddq_f32(float32x4_t a, float32x4_t b) {
#pragma clang fp contract(off)
    return a + b;
}
NC_INLINE float32x4_t vmulq_n_f32(float32x4_t a, float b) {
#pragma clang fp contract(off)
    return a * (float32x4_t){b, b, b, b};
}

/* Memory: unaligned, as NEON's loads and stores are. */
NC_INLINE float32x4_t vld1q_f32(const float *p) {
    float32x4_t v;
    memcpy(&v, p, sizeof v);
    return v;
}
NC_INLINE uint16x8_t vld1q_u16(const uint16_t *p) {
    uint16x8_t v;
    memcpy(&v, p, sizeof v);
    return v;
}
NC_INLINE void vst1q_u8(uint8_t *p, uint8x16_t v) { memcpy(p, &v, sizeof v); }
/* ST4: the four vectors interleaved, p[4i + k] = val[k][i]. */
NC_INLINE void vst4q_f32(float *p, float32x4x4_t t) {
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k)
            p[4 * i + k] = t.val[k][i];
}

#undef NC_INLINE
#endif
