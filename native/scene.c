/* scene.c - native replacements on the scene's hot paths (the base game's
 * addresses below, the others' in games/<id>/native/addresses.h). Each leaves the guest exactly as
 * the translated original does: memory, registers, the stack slots it wrote,
 * the MMX/XMM registers and the flags its translation last wrote. A call the
 * native path does not cover goes to the original whole.
 *
 * Switches (native_check.h): 1 (default) native, 0 the original, a alternate
 * every 10 s, c check (the original, memory put back, the native version,
 * compared; the original's results stay). */
#include <arm_neon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x86.h"
#include "funcs.h"
#include "native_check.h"
#include "native_seam.h"

#ifdef RECOMP_ADDRESS_HEADER
#include RECOMP_ADDRESS_HEADER
#endif
#ifndef HOMM5_IB_FILL_ORIG /* the base game, which has no address header */
#define HOMM5_IB_FILL_ORIG fn_0060d260
#define HOMM5_IB_NOBASE 0x011375e8u
#define HOMM5_POOL_ALLOC_ORIG fn_0060d810
#define HOMM5_CULL_TEST_ORIG fn_00a1a680
#define HOMM5_CULL_PUSH_ORIG fn_00a1a9f0
#define HOMM5_CULL_OBJECT_ORIG fn_00a1ab50
#define HOMM5_CULL_SIGNMASK 0x00f94980u
#endif

#pragma clang fp contract(off)

/* ---- 00507290: the dynamic 32-bit index buffer's fill ------------------
 * (the base game's 0060d260; thiscall, this the IB manager, then src (u16
 * indices), tris, base, nverts, mode; RET 0x14). Appends 3*tris indices
 * src[i] + base (0 under the "no base" flag) to the ring entry's locked index
 * buffer at this+0x10 (triangles) and keeps the fields DrawIndexedPrimitive
 * reads. The native path is the common call: the mode unchanged, no flag,
 * the entry already locked and room for every triangle - one chunk, no guest
 * call. Anything else (a flush, the ring's end, a Lock) runs the original.
 * 3.3% of the zoomed-out ToE map's main thread translated (the MMX loop with
 * its registers in memory). */
typedef struct {
    uint32_t self, src, tris, base, nverts, entry, pos, dst;
} IbFill;

/* Whether this call is the native path's, and its operands. */
static int ib_fill_plan(const X86 *c, IbFill *f) {
    const uint32_t E = c->r[4], self = c->r[1];
    f->self = self;
    f->src = rd32(E + 4u);
    f->tris = rd32(E + 8u);
    f->base = rd32(E + 12u);
    f->nverts = rd32(E + 16u);
    const int32_t n = (int32_t)(rd32(self + 8u) - rd32(self + 4u)) >> 2;
    if (n <= 0 || (int32_t)f->tris <= 0 || rd32(E + 20u) != rd32(self + 0x2cu) ||
        rd8(HOMM5_IB_NOBASE) != 0)
        return 0;
    const int32_t idx = (int32_t)rd32(self + 0x28u) % n; /* IDIV's remainder */
    f->entry = rd32(rd32(self + 4u) + (uint32_t)idx * 4u);
    f->pos = rd32(self + 0x10u);
    if ((int32_t)(rd32(self + 0x14u) - f->pos) < (int32_t)f->tris)
        return 0; /* more than the ring has left: the original's loop */
    const uint32_t locked = rd32(f->entry + 0x10u);
    if (!locked)
        return 0;
    f->dst = locked + f->pos * 12u;
    /* The original reads 4 indices before it writes them: an overlap would
     * read what it wrote. Never seen; left to it. */
    const uint64_t s0 = f->src, s1 = s0 + (uint64_t)f->tris * 6u, d0 = f->dst,
                   d1 = d0 + (uint64_t)f->tris * 12u;
    if (s0 < d1 && d0 < s1)
        return 0;
    return 1;
}

static void ib_fill_compute(X86 *c, const IbFill *f) {
    const uint32_t E = c->r[4], k = f->tris, cnt = 3u * k, add = f->base;
    /* The stack frame: PUSH EBP, SUB ESP,0x1c, PUSH EBX, PUSH ESI, PUSH EDI
     * and the locals, as the original leaves them. */
    wr32(E - 4u, c->r[5]);
    wr32(E - 0x24u, c->r[3]);
    wr32(E - 0x28u, c->r[6]);
    wr32(E - 0x2cu, c->r[7]);
    wr32(E - 0x20u, f->self);
    wr32(E - 8u, f->entry);
    wr32(E - 0xcu, k);
    wr32(E - 0x10u, k);
    wr32(E - 0x14u, f->dst);
    wr32(E - 0x18u, cnt);
    wr32(E - 0x1cu, add);
    wr32(E + 8u, 0); /* tris left */
    wr32(E + 0x14u, f->src);
    /* The fields: NumVertices (signed max), the last base and count, the position. */
    const uint32_t top = f->base + f->nverts, old = rd32(f->self + 0x1cu);
    wr32(f->self + 0x1cu, (int32_t)old > (int32_t)top ? old : top);
    wr32(f->self + 0x20u, f->base);
    wr32(f->self + 0x24u, f->nverts);
    const uint32_t pos = f->pos + k;
    wr32(f->self + 0x10u, pos);
    /* dst[i] = src[i] + add: PUNPCKLWD/HWD with zero and PADDD, 8 at a time. */
    const uint16_t *s = (const uint16_t *)(g_mem + f->src);
    uint8_t *d = g_mem + f->dst;
    const uint32x4_t vadd = vdupq_n_u32(add);
    uint32_t i = 0;
    for (; i + 8 <= cnt; i += 8) {
        const uint16x8_t v = vld1q_u16(s + i);
        vst1q_u8(d + i * 4u, vreinterpretq_u8_u32(vaddw_u16(vadd, vget_low_u16(v))));
        vst1q_u8(d + i * 4u + 16u, vreinterpretq_u8_u32(vaddw_u16(vadd, vget_high_u16(v))));
    }
    for (; i < cnt; ++i) {
        const uint32_t x = (uint32_t)s[i] + add;
        memcpy(d + i * 4u, &x, 4);
    }
    recomp_track_range(f->dst, cnt * 4u);
    if (cnt >= 4) {
        /* MM1 the add twice, MM0 zero, MM3/MM4 the last MMX group's pairs. */
        const uint32_t j = (cnt / 4u - 1u) * 4u;
        c->mm[0] = 0;
        c->mm[1] = (uint64_t)add | (uint64_t)add << 32;
        c->mm[3] = (uint64_t)(uint32_t)(s[j] + add) | (uint64_t)(uint32_t)(s[j + 1] + add) << 32;
        c->mm[4] = (uint64_t)(uint32_t)(s[j + 2] + add) | (uint64_t)(uint32_t)(s[j + 3] + add)
                                                              << 32;
        c->fpu_tag = 0xffffu; /* EMMS */
    }
    c->r[0] = k;
    c->r[1] = 0;
    c->r[2] = pos;
    /* TEST ECX,ECX of the last chunk; AF from CMP byte [flag],0 */
    c->eflags_cf = 0;
    c->eflags_of = 0;
    c->eflags_zf = 1;
    c->eflags_sf = 0;
    c->eflags_pf = 1;
    c->eflags_af = 0;
    c->eip = rd32(E);
    c->r[4] = E + 0x18u;
}

void homm5_ib_fill(X86 *c) {
    static int mode = -1, shown = -1;
    const int m = nc_mode(&mode, &shown, "RECOMP_HOMM5_NATIVE_IB", "ib", 1);
    if (m == NC_ORIGINAL) {
        HOMM5_IB_FILL_ORIG(c);
        return;
    }
    static unsigned long long calls, fast, fast_tris, tris;
    IbFill f;
    const int plan = ib_fill_plan(c, &f);
    tris += f.tris;
    fast += plan;
    fast_tris += plan ? f.tris : 0;
    if ((++calls & ((1ull << 23) - 1)) == 0)
        fprintf(stderr, "[ib] %llu calls, %.1f%% native (%.1f%% of the triangles)\n", calls,
                100.0 * (double)fast / (double)calls, 100.0 * (double)fast_tris / (double)tris);
    if (!plan) {
        HOMM5_IB_FILL_ORIG(c);
        return;
    }
    const uint32_t E = c->r[4], ret_exp = c->ret_expect;
    if (m != NC_CHECK) {
        ib_fill_compute(c, &f);
        if (c->eip != ret_exp)
            recomp_return(c);
        return;
    }
    static NcCheck k = {"ib", 0, 0, 0};
    if (rd32(E) != ret_exp) {
        ++k.skipped;
        HOMM5_IB_FILL_ORIG(c);
        return;
    }
    /* What either may write: the stack frame, the fields, the indices. */
    enum { STK_LO = 0x2c, STK_N = 0x2c + 0x18, FLD_N = 0x30 };
    const uint32_t nidx = f.tris * 12u;
    uint8_t stk0[STK_N], stko[STK_N], fld0[FLD_N], fldo[FLD_N];
    uint8_t *idx0 = malloc(nidx), *idxo = malloc(nidx);
    memcpy(stk0, g_mem + E - STK_LO, STK_N);
    memcpy(fld0, g_mem + f.self, FLD_N);
    memcpy(idx0, g_mem + f.dst, nidx);
    X86 n = *c;
    HOMM5_IB_FILL_ORIG(c);
    memcpy(stko, g_mem + E - STK_LO, STK_N);
    memcpy(fldo, g_mem + f.self, FLD_N);
    memcpy(idxo, g_mem + f.dst, nidx);
    memcpy(g_mem + E - STK_LO, stk0, STK_N);
    memcpy(g_mem + f.self, fld0, FLD_N);
    memcpy(g_mem + f.dst, idx0, nidx);
    ib_fill_compute(&n, &f);
    char why[200];
    int bad = nc_state_differs(c, &n, why, sizeof why);
    if (!bad && memcmp(stko, g_mem + E - STK_LO, STK_N) != 0) {
        int o = 0;
        while (stko[o] == g_mem[E - STK_LO + o])
            ++o;
        bad = 1, snprintf(why, sizeof why, "stack byte at E%+d", o - STK_LO);
    }
    if (!bad && memcmp(fldo, g_mem + f.self, FLD_N) != 0)
        bad = 1, snprintf(why, sizeof why, "the manager's fields");
    if (!bad && memcmp(idxo, g_mem + f.dst, nidx) != 0)
        bad = 1, snprintf(why, sizeof why, "the indices (%u triangles)", f.tris);
    memcpy(g_mem + E - STK_LO, stko, STK_N);
    memcpy(g_mem + f.self, fldo, FLD_N);
    memcpy(g_mem + f.dst, idxo, nidx);
    free(idx0);
    free(idxo);
    nc_report(&k, bad, "fill", why);
}

/* ---- 00507840: a range of a vertex pool (the base game's 0060d810) -----
 * (thiscall, this the pool, the vertex count; RET 4): returns the range, +0x1c
 * its first vertex, +0x24 its count; the pool's +0xc holds the vertex buffer
 * at +0xc and +0x18 the stride. Whoever asked writes the range next, in this
 * frame: its pages are opened for the write up front (d3d9_prewrite) instead
 * of faulting one by one - the format 5 writer, the particles, the map's
 * dynamic geometry, about 50 faults a frame on the zoomed-out ToE map. The
 * guest sees only the original. */
void homm5_pool_alloc(X86 *c) {
    const uint32_t self = c->r[1];
    HOMM5_POOL_ALLOC_ORIG(c);
    const uint32_t r = c->r[0];
    if (!r)
        return;
    const uint32_t vb = rd32(rd32(self + 0xcu) + 0xcu), stride = rd32(self + 0x18u);
    d3d9_prewrite(vb, rd32(r + 0x1cu) * stride, rd32(r + 0x24u) * stride);
}

/* ---- The cull tests: a sphere against the culler's planes -----------------
 * 00966e10, 00967180, 009672e0 (the base game's 00a1a680, 00a1a9f0,
 * 00a1ab50; thiscall, this the camera's culler, a pointer to {x, y, z, r};
 * RET 4; AL 1 visible, 0 outside). The centre goes through the culler's
 * current matrix slot (+0x400, 0x80 each), then for each plane (+0x404,
 * {a, b, c, d}, count +0x604) whose bit is set in the mask on top of the
 * mask stack (+0x608, top +0x688): outside when -r > d. 00967180 and 009672e0
 * also push the mask less the planes the sphere is wholly inside; 009672e0
 * keeps a "some plane has 0 > d" flag in AH and a stack byte. Transcribed
 * instruction by instruction from the .asm with the registers in locals and
 * the flags of the last flag-setting instruction only (the translation keeps
 * the XMM registers in memory and every flag live): 2.6% of the zoomed-out
 * ToE map's main thread translated. RECOMP_HOMM5_NATIVE_CULL. */
typedef struct {
    uint32_t x[8][4];
} Xmm;
static inline float lane_f(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static inline uint32_t f_lane(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
#define MOVSS_LD(d, a) (X.x[d][0] = rd32(a), X.x[d][1] = X.x[d][2] = X.x[d][3] = 0)
#define MOVAPS(d, s) memcpy(X.x[d], X.x[s], 16)
#define MULSS_M(d, a) (X.x[d][0] = f_lane(lane_f(X.x[d][0]) * rdf32(a)))
#define ADDSS_M(d, a) (X.x[d][0] = f_lane(lane_f(X.x[d][0]) + rdf32(a)))
#define ADDSS_R(d, s) (X.x[d][0] = f_lane(lane_f(X.x[d][0]) + lane_f(X.x[s][0])))
#define XMM_F(r) lane_f(X.x[r][0])

static void set_logic_flags(X86 *c, uint32_t r) { /* TEST / AND / OR / XOR, 32-bit */
    c->eflags_cf = 0;
    c->eflags_of = 0;
    c->eflags_zf = r == 0;
    c->eflags_sf = r >> 31;
    c->eflags_pf = parity8(r);
}

static void cull_test_compute(X86 *c) {
    const uint32_t E = c->r[4], self = c->r[1];
    Xmm X;
    memcpy(&X, c->xmm, sizeof X);
    uint32_t eax, ecx = self, edx, esi, edi;
    wr32(E - 4u, ecx);      /* PUSH ECX */
    wr32(E - 8u, c->r[6]);  /* PUSH ESI */
    wr32(E - 12u, c->r[7]); /* PUSH EDI */
    edi = ecx;
    edx = 1;
    eax = rd32(edi + 0x688u);
    esi = rd32(edi + eax * 4u + 0x608u);
    if (!esi) {
        set_logic_flags(c, esi);
        goto visible;
    }
    {
        const uint32_t s = rd32(E + 4u);
        ecx = s;
        eax = (rd32(edi + 0x400u) << 7) + edi;
        MOVSS_LD(2, s + 4u);
        MOVSS_LD(3, s);
        MOVAPS(4, 2);
        MOVSS_LD(1, s + 8u);
        MOVAPS(0, 3);
        MULSS_M(0, eax);
        MOVAPS(5, 3);
        ecx = 0;
        MULSS_M(4, eax + 0x4u);
        MOVAPS(6, 3);
        MULSS_M(5, eax + 0x10u);
        ADDSS_R(4, 0);
        MULSS_M(6, eax + 0x20u);
        MULSS_M(3, eax + 0x30u);
        MOVAPS(0, 1);
        MULSS_M(0, eax + 0x8u);
        ADDSS_R(4, 0);
        MOVAPS(0, 2);
        MULSS_M(0, eax + 0x14u);
        ADDSS_R(5, 0);
        MOVAPS(0, 1);
        MULSS_M(0, eax + 0x18u);
        ADDSS_M(4, eax + 0xcu);
        ADDSS_R(5, 0);
        MOVAPS(0, 2);
        MULSS_M(0, eax + 0x24u);
        MULSS_M(2, eax + 0x34u);
        ADDSS_M(5, eax + 0x1cu);
        ADDSS_R(6, 0);
        MOVAPS(0, 1);
        MULSS_M(0, eax + 0x28u);
        ADDSS_R(3, 2);
        MULSS_M(1, eax + 0x38u);
        ADDSS_R(6, 0);
        ADDSS_R(3, 1);
        ADDSS_M(6, eax + 0x2cu);
        ADDSS_M(3, eax + 0x3cu);
        eax = rd32(edi + 0x604u);
        wr32(E - 4u, eax); /* [ESP+8] */
        set_logic_flags(c, eax);
        if ((int32_t)eax <= 0)
            goto visible;
        MOVSS_LD(2, HOMM5_CULL_SIGNMASK);
        eax = edi + 0x40cu;
        const int32_t count = (int32_t)rd32(E - 4u);
        const float r = rdf32(s + 0xcu);
        for (;;) {
            if (esi & edx) {
                MOVAPS(0, 4);
                MULSS_M(0, eax - 8u);
                MOVAPS(1, 5);
                MULSS_M(1, eax - 4u);
                ADDSS_R(1, 0);
                MOVAPS(0, 6);
                MULSS_M(0, eax);
                ADDSS_R(1, 0);
                MOVAPS(0, 3);
                MULSS_M(0, eax + 4u);
                ADDSS_R(1, 0);
                MOVSS_LD(0, s + 0xcu);
                for (int l = 0; l < 4; ++l)
                    X.x[0][l] ^= X.x[2][l]; /* XORPS */
                (void)r;
                if (XMM_F(0) > XMM_F(1)) { /* COMISS; JA: ordered and above */
                    /* XOR AL,AL; AF from the COMISS */
                    eax &= 0xffffff00u;
                    set_logic_flags(c, 0);
                    c->eflags_af = 0;
                    ecx = rd32(E - 4u);
                    goto done;
                }
            }
            ++ecx;
            eax += 0x10u;
            edx += edx;
            { /* CMP ECX,EDI (EDI the count) */
                const uint32_t a = ecx, b = (uint32_t)count, rr = a - b;
                c->eflags_cf = a < b;
                c->eflags_of = ((a ^ b) & (a ^ rr)) >> 31;
                c->eflags_af = ((a ^ b ^ rr) >> 4) & 1u;
                c->eflags_zf = rr == 0;
                c->eflags_sf = rr >> 31;
                c->eflags_pf = parity8(rr);
            }
            if ((int32_t)ecx >= count)
                break;
        }
    }
visible:
    eax = (eax & 0xffffff00u) | 1u;
    ecx = rd32(E - 4u);
done:
    c->r[0] = eax;
    c->r[1] = ecx;
    c->r[2] = edx;
    memcpy(c->xmm, &X, sizeof X);
    c->eip = rd32(E);
    c->r[4] = E + 8u;
}

/* 00967180: the octree node's test, pushing the narrowed mask. */
static void cull_push_compute(X86 *c) {
    const uint32_t E = c->r[4], self = c->r[1];
    Xmm X;
    memcpy(&X, c->xmm, sizeof X);
    uint32_t eax, ecx = self, edx, esi, edi;
    wr32(E - 4u, ecx); /* PUSH ECX; MOV [ESP],ECX */
    const uint32_t s = rd32(E + 4u);
    eax = s;
    edx = (rd32(ecx + 0x400u) << 7) + ecx;
    MOVSS_LD(2, s + 4u);
    MOVSS_LD(4, s);
    MOVAPS(5, 2);
    MOVSS_LD(1, s + 8u);
    MOVAPS(0, 4);
    MULSS_M(0, edx);
    MOVAPS(6, 4);
    wr32(E - 8u, c->r[6]); /* PUSH ESI */
    MULSS_M(5, edx + 0x4u);
    MOVAPS(7, 4);
    wr32(E - 12u, c->r[7]); /* PUSH EDI */
    MULSS_M(6, edx + 0x10u);
    edi = 0;
    ADDSS_R(5, 0);
    MULSS_M(7, edx + 0x20u);
    MULSS_M(4, edx + 0x30u);
    MOVAPS(0, 1);
    MULSS_M(0, edx + 0x8u);
    MOVSS_LD(3, s + 0xcu);
    eax = rd32(ecx + 0x688u);
    ADDSS_R(5, 0);
    MOVAPS(0, 2);
    MULSS_M(0, edx + 0x14u);
    esi = rd32(ecx + eax * 4u + 0x608u);
    eax = rd32(ecx + 0x604u);
    wr32(E + 4u, eax); /* [ESP+0x10] */
    ADDSS_R(6, 0);
    MOVAPS(0, 1);
    MULSS_M(0, edx + 0x18u);
    ADDSS_M(5, edx + 0xcu);
    ADDSS_R(6, 0);
    MOVAPS(0, 2);
    MULSS_M(0, edx + 0x24u);
    MULSS_M(2, edx + 0x34u);
    ADDSS_R(7, 0);
    MOVAPS(0, 1);
    MULSS_M(0, edx + 0x28u);
    MULSS_M(1, edx + 0x38u);
    ADDSS_R(4, 2);
    ADDSS_R(7, 0);
    ADDSS_M(6, edx + 0x1cu);
    ADDSS_R(4, 1);
    ADDSS_M(7, edx + 0x2cu);
    ADDSS_M(4, edx + 0x3cu);
    edx = 1;
    set_logic_flags(c, eax);
    if ((int32_t)eax > 0) {
        MOVSS_LD(2, HOMM5_CULL_SIGNMASK);
        ecx += 0x40cu;
        for (;;) {
            if (esi & edx) {
                MOVAPS(0, 5);
                MOVAPS(1, 6);
                MULSS_M(0, ecx - 8u);
                MULSS_M(1, ecx - 4u);
                ADDSS_R(1, 0);
                MOVAPS(0, 7);
                MULSS_M(0, ecx);
                ADDSS_R(1, 0);
                MOVAPS(0, 4);
                MULSS_M(0, ecx + 4u);
                ADDSS_R(1, 0);
                MOVAPS(0, 3);
                for (int l = 0; l < 4; ++l)
                    X.x[0][l] ^= X.x[2][l];
                if (XMM_F(0) > XMM_F(1)) {
                    /* POP EDI; XOR AL,AL; POP ESI; POP ECX */
                    c->r[0] = eax & 0xffffff00u;
                    set_logic_flags(c, 0);
                    c->eflags_af = 0;
                    c->r[1] = rd32(E - 4u);
                    c->r[2] = edx;
                    memcpy(c->xmm, &X, sizeof X);
                    c->eip = rd32(E);
                    c->r[4] = E + 8u;
                    return;
                }
                if (XMM_F(1) > XMM_F(3)) { /* COMISS XMM1,XMM3; JBE skips */
                    eax = ~edx;
                    esi &= eax;
                    eax = rd32(E + 4u);
                }
            }
            ++edi;
            ecx += 0x10u;
            edx += edx;
            {
                const uint32_t a = edi, b = eax, rr = a - b;
                c->eflags_cf = a < b;
                c->eflags_of = ((a ^ b) & (a ^ rr)) >> 31;
                c->eflags_af = ((a ^ b ^ rr) >> 4) & 1u;
                c->eflags_zf = rr == 0;
                c->eflags_sf = rr >> 31;
                c->eflags_pf = parity8(rr);
            }
            if ((int32_t)edi >= (int32_t)eax)
                break;
        }
        ecx = rd32(E - 4u);
    }
    /* MOV EAX,[ECX+0x688]; INC EAX; stores; MOV AL,1; POP ECX */
    eax = rd32(ecx + 0x688u);
    {
        const uint32_t a = eax, rr = a + 1u;
        c->eflags_of = ((~(a ^ 1u) & (a ^ rr)) >> 31) & 1u;
        c->eflags_af = ((a ^ 1u ^ rr) >> 4) & 1u;
        c->eflags_zf = rr == 0;
        c->eflags_sf = rr >> 31;
        c->eflags_pf = parity8(rr);
        eax = rr;
    }
    wr32(ecx + 0x688u, eax);
    wr32(ecx + eax * 4u + 0x608u, esi);
    c->r[0] = (eax & 0xffffff00u) | 1u;
    c->r[1] = rd32(E - 4u);
    c->r[2] = edx;
    memcpy(c->xmm, &X, sizeof X);
    c->eip = rd32(E);
    c->r[4] = E + 8u;
}

/* 009672e0: an object's test, pushing the narrowed mask, with the AH flag. */
static void cull_object_compute(X86 *c) {
    const uint32_t E = c->r[4], self = c->r[1];
    Xmm X;
    memcpy(&X, c->xmm, sizeof X);
    uint32_t eax, ecx = self, edx, esi, edi;
    /* SUB ESP,8: ESP = E-8 */
    const uint32_t s = rd32(E + 4u);
    eax = s;
    edx = (rd32(ecx + 0x400u) << 7) + ecx;
    wr32(E - 8u, ecx);
    MOVSS_LD(4, s + 4u);
    MOVSS_LD(1, s);
    MOVAPS(5, 4);
    MOVSS_LD(2, s + 8u);
    MOVAPS(0, 1);
    MULSS_M(0, edx);
    MOVAPS(6, 4);
    wr32(E - 12u, c->r[6]); /* PUSH ESI */
    MULSS_M(5, edx + 0x4u);
    MOVAPS(7, 4);
    wr32(E - 16u, c->r[7]); /* PUSH EDI */
    MULSS_M(6, edx + 0x14u);
    edi = rd32(ecx + 0x604u);
    ADDSS_R(5, 0);
    MULSS_M(7, edx + 0x24u);
    wr32(E - 4u, edi); /* [ESP+0xc] */
    MULSS_M(4, edx + 0x34u);
    MOVAPS(0, 2);
    MULSS_M(0, edx + 0x8u);
    MOVSS_LD(3, s + 0xcu);
    eax = rd32(ecx + 0x688u);
    ADDSS_R(5, 0);
    MOVAPS(0, 1);
    MULSS_M(0, edx + 0x10u);
    esi = rd32(ecx + eax * 4u + 0x608u);
    eax &= 0xffff00ffu;      /* XOR AH,AH */
    set_logic_flags(c, edi); /* TEST EDI,EDI */
    ADDSS_R(6, 0);
    MOVAPS(0, 2);
    MULSS_M(0, edx + 0x18u);
    ADDSS_M(5, edx + 0xcu);
    ADDSS_R(6, 0);
    MOVAPS(0, 1);
    MULSS_M(0, edx + 0x20u);
    MULSS_M(1, edx + 0x30u);
    ADDSS_M(6, edx + 0x1cu);
    ADDSS_R(7, 0);
    MOVAPS(0, 2);
    MULSS_M(0, edx + 0x28u);
    ADDSS_R(4, 1);
    MULSS_M(2, edx + 0x38u);
    ADDSS_R(7, 0);
    ADDSS_R(4, 2);
    ADDSS_M(7, edx + 0x2cu);
    ADDSS_M(4, edx + 0x3cu);
    const int32_t count = (int32_t)edi;
    edx = 1;
    edi = 0;
    int out = 0;
    if (count > 0) {
        ecx += 0x40cu;
        memset(X.x[2], 0, 16); /* XORPS XMM2,XMM2 */
        const uint32_t k = HOMM5_CULL_SIGNMASK;
        for (;;) {
            if (esi & edx) {
                MOVAPS(0, 5);
                MOVAPS(1, 6);
                MULSS_M(0, ecx - 8u);
                MULSS_M(1, ecx - 4u);
                ADDSS_R(1, 0);
                MOVAPS(0, 7);
                MULSS_M(0, ecx);
                ADDSS_R(1, 0);
                MOVAPS(0, 4);
                MULSS_M(0, ecx + 4u);
                ADDSS_R(1, 0);
                MOVAPS(0, 3);
                for (int l = 0; l < 4; ++l)
                    X.x[0][l] ^= rd32(k + 4u * (uint32_t)l); /* XORPS XMM0,[k] */
                if (XMM_F(0) > XMM_F(1)) {
                    out = 1;
                    break;
                }
                /* COMISS XMM2,XMM1; SETA AL; OR AH,AL */
                const uint32_t al = 0.0f > XMM_F(1) ? 1u : 0u;
                eax = (eax & 0xffffff00u) | al;
                const uint32_t ah = ((eax >> 8) | al) & 0xffu;
                eax = (eax & 0xffff00ffu) | ah << 8;
                /* COMISS XMM1,XMM3; MOV [ESP+0x17],AH */
                wr8(E + 7u, (uint8_t)ah);
                if (XMM_F(1) > XMM_F(3)) {
                    eax = ~edx;
                    esi &= eax;
                    eax = (eax & 0xffff00ffu) | (uint32_t)rd8(E + 7u) << 8;
                }
            }
            ++edi;
            ecx += 0x10u;
            edx += edx;
            if ((int32_t)edi >= count)
                break;
        }
        if (!out)
            ecx = rd32(E - 8u);
    }
    if (out) {
        /* POP EDI; XOR AL,AL; POP ESI */
        eax &= 0xffffff00u;
    } else {
        eax = rd32(ecx + 0x688u) + 1u;
        wr32(ecx + 0x688u, eax);
        wr32(ecx + eax * 4u + 0x608u, esi);
        eax = (eax & 0xffffff00u) | 1u;
    }
    { /* ADD ESP,8 from E-8 */
        const uint32_t a = E - 8u, b = 8u, rr = a + b;
        c->eflags_cf = rr < a;
        c->eflags_of = ((~(a ^ b) & (a ^ rr)) >> 31) & 1u;
        c->eflags_af = ((a ^ b ^ rr) >> 4) & 1u;
        c->eflags_zf = rr == 0;
        c->eflags_sf = rr >> 31;
        c->eflags_pf = parity8(rr);
    }
    c->r[0] = eax;
    c->r[1] = ecx;
    c->r[2] = edx;
    memcpy(c->xmm, &X, sizeof X);
    c->eip = rd32(E);
    c->r[4] = E + 8u;
}

/* The three tests' switch and check: memory either may write is the stack
 * [E-16, E+8) and the mask stack with its top, this+0x608 .. this+0x68c. */
static void cull_run(X86 *c, void (*orig)(X86 *), void (*native)(X86 *), const char *what,
                     NcCheck *k) {
    static int mode = -1, shown = -1;
    const int m = nc_mode(&mode, &shown, "RECOMP_HOMM5_NATIVE_CULL", "cull", 1);
    if (m == NC_ORIGINAL) {
        orig(c);
        return;
    }
    const uint32_t E = c->r[4], ret_exp = c->ret_expect;
    if (m != NC_CHECK) {
        native(c);
        if (c->eip != ret_exp)
            recomp_return(c);
        return;
    }
    if (rd32(E) != ret_exp) {
        ++k->skipped;
        orig(c);
        return;
    }
    enum { SN = 24, CN = 0x84 };
    const uint32_t self = c->r[1];
    uint8_t s0[SN], so[SN], c0[CN], co[CN];
    memcpy(s0, g_mem + E - 16u, SN);
    memcpy(c0, g_mem + self + 0x608u, CN);
    X86 n = *c;
    orig(c);
    memcpy(so, g_mem + E - 16u, SN);
    memcpy(co, g_mem + self + 0x608u, CN);
    memcpy(g_mem + E - 16u, s0, SN);
    memcpy(g_mem + self + 0x608u, c0, CN);
    native(&n);
    char why[200];
    int bad = nc_state_differs(c, &n, why, sizeof why);
    if (!bad && memcmp(so, g_mem + E - 16u, SN) != 0) {
        int o = 0;
        while (so[o] == g_mem[E - 16u + o])
            ++o;
        bad = 1, snprintf(why, sizeof why, "stack byte at E%+d", o - 16);
    }
    if (!bad && memcmp(co, g_mem + self + 0x608u, CN) != 0)
        bad = 1, snprintf(why, sizeof why, "the mask stack");
    memcpy(g_mem + E - 16u, so, SN);
    memcpy(g_mem + self + 0x608u, co, CN);
    nc_report(k, bad, what, why);
}
void homm5_cull_test(X86 *c) {
    static NcCheck k = {"cull test", 0, 0, 0};
    cull_run(c, HOMM5_CULL_TEST_ORIG, cull_test_compute, "test", &k);
}
void homm5_cull_push(X86 *c) {
    static NcCheck k = {"cull push", 0, 0, 0};
    cull_run(c, HOMM5_CULL_PUSH_ORIG, cull_push_compute, "push", &k);
}
void homm5_cull_object(X86 *c) {
    static NcCheck k = {"cull object", 0, 0, 0};
    cull_run(c, HOMM5_CULL_OBJECT_ORIG, cull_object_compute, "object", &k);
}
