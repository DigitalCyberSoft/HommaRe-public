// Floating point that network play depends on, the same on every host. Heroes
// V's peers each compute the game from the commands they exchange, so the
// translated arithmetic must give one answer on x86_64, arm64 and wasm32:
//
//   - the x87 and SSE conversions to integer, where a C cast of a NaN or an
//     out-of-range value is undefined (x86_64 gives the "integer indefinite"
//     0x80000000, arm64 saturates) and the helpers must give x86's answer;
//   - the control word's rounding and precision, computed explicitly;
//   - no FMA contraction (CMakeLists.txt's -ffp-contract=off).
//
// Each part is checked against the x86 answer and folded into a hash, which
// must come out the same on every host.
//
// Standalone on purpose (x86.h, nothing else), so the same file builds for a
// cross target and runs under an emulator:
//   clang++ -std=c++20 -O2 -ffp-contract=off -I kit/runtime \
//     kit/runtime/tests/fp_determinism_tests.cpp
#include "x86.h"

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_checks = 0, g_failures = 0;
static bool check(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static bool check(bool ok, const char *fmt, ...) {
    ++g_checks;
    va_list ap;
    va_start(ap, fmt);
    char msg[512];
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (!ok)
        ++g_failures;
    if (!ok || getenv("FP_VERBOSE"))
        printf("  [%s] %s\n", ok ? "ok" : "FAIL", msg);
    return ok;
}

// FNV-1a over the bits of every result.
struct Hash {
    uint64_t h = 0xcbf29ce484222325ull;
    void bytes(const void *p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            h ^= ((const uint8_t *)p)[i];
            h *= 0x100000001b3ull;
        }
    }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            const uint8_t b = (uint8_t)(v >> (8 * i));
            bytes(&b, 1);
        }
    }
    void f64(double v) {
        uint64_t b;
        memcpy(&b, &v, 8);
        u64(b);
    }
};

static uint64_t bits(double v) {
    uint64_t b;
    memcpy(&b, &v, 8);
    return b;
}
static uint32_t bitsf(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}

// A context with the given x87 control word.
static X86 ctx(uint16_t cw) {
    X86 c;
    memset(&c, 0, sizeof c);
    c.fpu_cw = cw;
    c.fpu_tag = 0xffff;
    return c;
}
enum : uint16_t {
    RC_NEAR = 0x037f,
    RC_DOWN = 0x077f,
    RC_UP = 0x0b7f,
    RC_CHOP = 0x0f7f,
    PC_24 = 0x007f
};

// Opaque to the optimiser, so nothing below is folded at compile time with
// the compiler's own arithmetic instead of the target's.
template <typename T> static T opaque(T v) {
    volatile T x = v;
    return x;
}

// ------------------------------------------------------- the exact part --

static void test_conversions(Hash &h) {
    const double nan = opaque((double)NAN), inf = opaque((double)INFINITY);
    X86 c = ctx(RC_NEAR);
    struct {
        double v;
        int32_t near, down, up, chop;
    } cases[] = {
        {2.5, 2, 2, 3, 2},
        {3.5, 4, 3, 4, 3},
        {-2.5, -2, -3, -2, -2},
        {-0.5, 0, -1, 0, 0},
        {2147483647.4, 2147483647, 2147483647, INT32_MIN, 2147483647},
        {-2147483648.6, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN},
        {2147483648.0, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN},
        {1e10, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN},
        {-1e300, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN},
    };
    for (auto &k : cases) {
        const double v = opaque(k.v);
        const uint16_t cws[4] = {RC_NEAR, RC_DOWN, RC_UP, RC_CHOP};
        const int32_t want[4] = {k.near, k.down, k.up, k.chop};
        for (int m = 0; m < 4; ++m) {
            c = ctx(cws[m]);
            const int32_t got = fto_i32(&c, v);
            check(got == want[m], "FIST %.1f, RC %d: %d (x86 %d)", k.v, m, got, want[m]);
            h.u64((uint32_t)got);
        }
    }
    c = ctx(RC_NEAR);
    check(fto_i32(&c, nan) == INT32_MIN && (c.fpu_sw & 1), "FIST NaN: the integer indefinite, IE");
    check(fto_i32(&c, inf) == INT32_MIN, "FIST +inf: indefinite");
    check(fto_i16(&c, opaque(32767.5)) == INT16_MIN,
          "FIST word 32767.5 rounds to 32768: indefinite");
    check(fto_i16(&c, opaque(-32768.4)) == -32768, "FIST word -32768.4");
    check(fto_i64(&c, opaque(9.3e18)) == INT64_MIN, "FIST qword 9.3e18: indefinite");
    check(fto_i64(&c, opaque(-9223372036854775808.0)) == INT64_MIN, "FIST qword -2^63 fits");
    h.u64((uint64_t)fto_i64(&c, opaque(4503599627370497.0)));

    // SSE: CVTTSS2SI / CVTTSD2SI / CVTTPS2DQ truncate; CVTPS2PI rounds to even.
    struct {
        float v;
        int32_t want;
    } ss[] = {{3e9f, INT32_MIN},
              {-3e9f, INT32_MIN},
              {2147483520.0f, 2147483520},
              {-2147483648.0f, INT32_MIN},
              {-2147483520.0f, -2147483520},
              {-1.9f, -1},
              {(float)NAN, INT32_MIN},
              {(float)INFINITY, INT32_MIN}};
    for (auto &k : ss) {
        const int32_t got = recomp_sse_cvtt_i32(opaque(k.v));
        check(got == k.want, "CVTTSS2SI %g: %d (x86 %d)", (double)k.v, got, k.want);
        h.u64((uint32_t)got);
    }
    check(recomp_sse_cvt_i32(opaque(2.5f)) == 2 && recomp_sse_cvt_i32(opaque(3.5f)) == 4 &&
              recomp_sse_cvt_i32(opaque(-2.5f)) == -2 &&
              recomp_sse_cvt_i32(opaque(5e9f)) == INT32_MIN,
          "CVTPS2PI: nearest-even, indefinite out of range");
    struct {
        double v;
        int32_t want;
    } sd[] = {{2147483647.9, 2147483647}, {-2147483648.9, INT32_MIN}, {2147483648.0, INT32_MIN},
              {-2147483649.0, INT32_MIN}, {nan, INT32_MIN},           {-0.99, 0}};
    for (auto &k : sd) {
        const int32_t got = recomp_sse_cvtt_i32d(opaque(k.v));
        check(got == k.want, "CVTTSD2SI %.2f: %d (x86 %d)", k.v, got, k.want);
        h.u64((uint32_t)got);
    }
    // FISTTP truncates whatever RC says.
    c = ctx(RC_UP);
    c.st[0] = opaque(-7.9);
    check(fistt_i32(&c) == -7, "FISTTP -7.9 under RC up: -7");
    c.st[0] = opaque(5e9);
    check(fistt_i32(&c) == INT32_MIN, "FISTTP 5e9: indefinite");
}

// FSCALE's scale reaches ldexp as an int: out of int's range a C cast would
// give x86_64 0 and arm64 infinity for the same 1 * 2^1e10.
static void test_fscale(Hash &h) {
    struct {
        double a, b, want;
    } cases[] = {
        {3.0, 2.7, 12.0},
        {3.0, -1.5, 1.5},
        {1.0, 1e10, (double)INFINITY},
        {-1.0, 1e10, -(double)INFINITY},
        {1.0, -1e10, 0.0},
        {1.0, 3e9, (double)INFINITY},
        {5.0, -(double)INFINITY, 0.0},
        {5.0, (double)INFINITY, (double)INFINITY},
        {0x1p-1074, 2000.0, 0x1p926},
        {0x1p1023, -2100.0, 0.0},
    };
    for (auto &k : cases) {
        const double got = fscale(opaque(k.a), opaque(k.b));
        check(bits(got) == bits(k.want), "FSCALE %g by 2^%g: %g (x86 %g)", k.a, k.b, got, k.want);
        h.f64(got);
    }
    X86 c = ctx(RC_NEAR);
    check(bits(fx87_exact(&c, fscale(opaque(0.0), opaque((double)INFINITY)))) ==
              0xfff8000000000000ull,
          "FSCALE 0 by 2^inf: invalid");
    check(fscale(opaque(-0.0), opaque(5.0)) == 0.0 &&
              std::signbit(fscale(opaque(-0.0), opaque(5.0))),
          "FSCALE keeps -0");
}

static void test_rounding(Hash &h) {
    // FRNDINT per RC, ties to even, exact above 2^52.
    X86 c = ctx(RC_NEAR);
    const double r[] = {0.5, 1.5, 2.5, -0.5, -1.5, 4503599627370497.0, 1e300};
    for (double v : r)
        h.f64(fround_cw(&c, opaque(v)));
    check(fround_cw(&c, opaque(2.5)) == 2.0 && fround_cw(&c, opaque(-1.5)) == -2.0 &&
              fround_cw(&c, opaque(4503599627370497.0)) == 4503599627370497.0,
          "FRNDINT nearest-even");
    // PC = 24 bits: the basic operations round to single.
    c = ctx(PC_24);
    const double one_eps = opaque(1.0) + opaque(0x1p-30);
    check(fx87(&c, one_eps) == 1.0, "PC=24: 1 + 2^-30 rounds to 1");
    c = ctx(RC_NEAR);
    check(fx87(&c, one_eps) == one_eps, "PC=64 (a double here): 1 + 2^-30 stays");
    // FST to a float under each RC.
    const double v = opaque(1.0 + 0x1p-40);
    const uint16_t cws[4] = {RC_NEAR, RC_DOWN, RC_UP, RC_CHOP};
    const uint32_t want[4] = {0x3f800000u, 0x3f800000u, 0x3f800001u, 0x3f800000u};
    for (int m = 0; m < 4; ++m) {
        c = ctx(cws[m]);
        const uint32_t got = bitsf(fto_float(&c, v));
        check(got == want[m], "FST float 1+2^-40, RC %d: %08x", m, got);
        h.u64(got);
    }
    // NaN results are the x87's indefinite, not the host's default NaN
    // (x86 0xfff8..., arm64 0x7ff8...).
    c = ctx(RC_NEAR);
    const double inv = fx87(&c, opaque(0.0) / opaque(0.0));
    check(bits(inv) == 0xfff8000000000000ull, "x87 0/0: the indefinite %016llx",
          (unsigned long long)bits(inv));
    check(bits(recomp_sse_sqrt(opaque(-1.0))) == 0xfff8000000000000ull,
          "SQRTSD -1: the indefinite");
    check(bitsf(recomp_sse_sqrtf(opaque(-4.0f))) == 0xffc00000u, "SQRTSS -4: the indefinite");
    h.f64(inv);
}

// A multiply and an add in one expression must round twice. With contraction
// (clang's default) arm64 fuses them into one FMA and gets -2^-54 here.
static void test_contraction(Hash &h) {
    const double a = opaque(1.0 + 0x1p-27), b = opaque(1.0 - 0x1p-27), c = opaque(-1.0);
    const double r = a * b + c;
    check(r == 0.0, "a*b+c rounds the product first (no FMA): %a", r);
    const float af = opaque(1.0f + 0x1p-13f), bf = opaque(1.0f - 0x1p-13f), cf = opaque(-1.0f);
    const float rf = af * bf + cf;
    check(rf == 0.0f, "float a*b+c: %a", (double)rf);
    h.f64(r);
    h.f64((double)rf);
}

// The hash of everything above as x86_64 Linux computes it; every host must agree.
static const uint64_t EXACT_HASH = 0x576cfde6d47af9b1ull;

int main() {
    Hash h;
    test_conversions(h);
    test_fscale(h);
    test_rounding(h);
    test_contraction(h);
    printf("fp determinism hash  %016llx\n", (unsigned long long)h.h);
    check(h.h == EXACT_HASH, "the hash x86_64 computes (%016llx)", (unsigned long long)EXACT_HASH);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
