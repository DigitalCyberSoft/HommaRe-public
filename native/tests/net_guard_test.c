/* native/tests/net_guard_test.c - unit tests for native/net_guard.c, the
 * bounded GameSpy string reader (README "Playing online safely").
 *
 * The test stands in mock clDataList readers for the game's two: an unbounded
 * one that copies a test string byte for byte into the destination (the
 * overflow), and a bounded one that strncpy-s with the size the guard passes.
 * Both move the guest stack exactly as the real readers do (thiscall, RET 0x8
 * and RET 0xc), so the guard's frame arithmetic is exercised for real. A
 * canary byte just past each destination buffer catches an overflow.
 *
 * Build and run (from the worktree root):
 *   cc -I kit/runtime -o /tmp/net_guard_test \
 *      native/tests/net_guard_test.c native/net_guard.c && /tmp/net_guard_test
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "x86.h"

/* ---- minimal runtime the x86.h accessors link against ---- */
uint8_t *g_mem;
uint32_t g_watch_base, g_watch_len;
RecompDirty g_dirty[RECOMP_DIRTY_SLOTS];
uint32_t g_dirty_count;
uint32_t g_track_base, g_track_len;
uint8_t *g_track_pages;
const int recomp_write_track_protect = 0;
const int recomp_fixed_guest_base = 0;
void recomp_null_access(uint32_t a, int w) { (void)a; (void)w; }
void recomp_watch_hit(uint32_t a, uint32_t n, uint64_t v) { (void)a; (void)n; (void)v; }

/* ---- the field the mock readers serve, and the guard under test ---- */
void homm5_gs_str_read(X86 *c);
static const char *g_field[8]; /* NULL = the element is absent */

/* The readers return a bool the way the game's do: SETcc AL, the rest of EAX
 * left as it was. */
static void set_al(X86 *c, uint32_t v) {
    c->r[R_EAX] = (c->r[R_EAX] & 0xffffff00u) | (v & 0xffu);
}

/* Common reader entry: list in ECX (ignored), [esp+4]=dst, [esp+8]=index.
 * Returns 1 in EAX when the element is present (and, bounded, when it fit). */
static int reader_begin(X86 *c, uint32_t *dst, uint32_t *index) {
    const uint32_t esp = c->r[R_ESP];
    *dst = rd32(esp + 4);
    *index = rd32(esp + 8);
    return 1;
}
static void reader_ret(X86 *c, uint32_t pop) {
    const uint32_t esp = c->r[R_ESP];
    c->eip = rd32(esp);
    c->r[R_ESP] = esp + 4 + pop;
}

/* Unbounded: the vulnerable copy (byte loop, no limit). RET 0x8. */
void fn_004432d0(X86 *c) {
    uint32_t dst, index;
    reader_begin(c, &dst, &index);
    const char *s = index < 8 ? g_field[index] : NULL;
    if (!s) {
        set_al(c, 0);
        reader_ret(c, 8);
        return;
    }
    for (const char *p = s;; ++p) {
        wr8(dst++, (uint8_t)*p);
        if (!*p)
            break;
    }
    set_al(c, 1);
    reader_ret(c, 8);
}

/* Bounded: strncpy into size bytes, forced terminator. [esp+0xc]=size.
 * RET 0xc. Returns 1 only when the whole string fit. */
void fn_00443390(X86 *c) {
    uint32_t dst, index;
    reader_begin(c, &dst, &index);
    const uint32_t size = rd32(c->r[R_ESP] + 0xc);
    const char *s = index < 8 ? g_field[index] : NULL;
    if (!s || size == 0) {
        set_al(c, 0);
        reader_ret(c, 0xc);
        return;
    }
    uint32_t i = 0;
    for (; i < size && s[i]; ++i)
        wr8(dst + i, (uint8_t)s[i]);
    int fit = (i < size); /* a NUL still inside the buffer */
    if (i >= size)
        i = size - 1;
    wr8(dst + i, 0);
    set_al(c, (uint32_t)fit);
    reader_ret(c, 0xc);
}

/* ---- the harness ---- */
enum { CTX = 0x1000, DST = 0x2000, DST_CAP = 0x100, STACK_TOP = 0x100000 };
static int failures;

/* Lay a reader frame as the game's caller does: push index, push dst, push a
 * return address, point ESP at it, run the guard, return the EAX result. The
 * canary byte at DST+size must survive. */
static uint32_t call_guard(uint32_t ret, uint32_t dst, uint32_t index, uint32_t canary_at) {
    X86 c;
    memset(&c, 0, sizeof c);
    c.r[R_EAX] = 0xdeadbe00u; /* the game leaves junk above AL */
    uint32_t esp = STACK_TOP;
    esp -= 4; wr32(esp, index);
    esp -= 4; wr32(esp, dst);
    esp -= 4; wr32(esp, ret);
    c.r[R_ESP] = esp;
    c.r[R_ECX] = CTX;
    const uint32_t entry = esp;
    if (canary_at)
        wr8(canary_at, 0xAA);
    homm5_gs_str_read(&c);
    /* The reader is caller-cleaned: ESP back to entry + 12, EIP at ret. */
    if (c.eip != ret) {
        printf("  FAIL: continuation %08x, expected %08x\n", c.eip, ret);
        failures++;
    }
    if (c.r[R_ESP] != entry + 12) {
        printf("  FAIL: esp %08x, expected %08x\n", c.r[R_ESP], entry + 12);
        failures++;
    }
    return c.r[R_EAX] & 0xffu;
}

static void expect(const char *name, int cond) {
    printf("  %s: %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond)
        failures++;
}

/* PLAYERINFO field 0 -> a 12-byte buffer: return address 0x0042a6a8. */
#define SITE_12 0x0042a6a8u
/* An uncatalogued site: the guard must leave it to the unbounded reader. */
#define SITE_UNKNOWN 0x00401234u

static void guest_str(uint32_t at, char *out, uint32_t cap) {
    uint32_t i = 0;
    for (; i + 1 < cap; ++i) {
        char ch = (char)rd8(at + i);
        out[i] = ch;
        if (!ch)
            return;
    }
    out[i] = 0;
}

int main(void) {
    g_mem = calloc(1, 0x200000);
    if (!g_mem) {
        perror("calloc");
        return 2;
    }
    setenv("RECOMP_HOMM5_NET_GUARD", "1", 1);
    char got[256];

    printf("normal (fits):\n");
    g_field[0] = "alice";
    memset(g_mem + DST, 0xCC, DST_CAP);
    uint32_t r = call_guard(SITE_12, DST, 0, DST + 12);
    guest_str(DST, got, sizeof got);
    expect("result is success", r == 1);
    expect("string copied whole", strcmp(got, "alice") == 0);
    expect("canary past buffer intact", rd8(DST + 12) == 0xAA);

    printf("maximum (11 chars into 12):\n");
    g_field[0] = "abcdefghijk"; /* 11 chars + NUL = 12 */
    memset(g_mem + DST, 0xCC, DST_CAP);
    r = call_guard(SITE_12, DST, 0, DST + 12);
    guest_str(DST, got, sizeof got);
    expect("result is success", r == 1);
    expect("string copied whole", strcmp(got, "abcdefghijk") == 0);
    expect("canary intact", rd8(DST + 12) == 0xAA);

    printf("over-long (attack: 40 chars into 12):\n");
    g_field[0] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    memset(g_mem + DST, 0xCC, DST_CAP);
    /* The drop must be logged: catch stderr in a file around the call. */
    char logpath[] = "/tmp/net_guard_test.XXXXXX";
    const int logfd = mkstemp(logpath);
    fflush(stderr);
    const int saved = dup(2);
    dup2(logfd, 2);
    r = call_guard(SITE_12, DST, 0, DST + 12);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    char logged[256] = {0};
    pread(logfd, logged, sizeof logged - 1, 0);
    close(logfd);
    unlink(logpath);
    guest_str(DST, got, sizeof got);
    expect("cut logged once", strstr(logged, "cut to 12 bytes") != NULL);
    expect("result is success (message goes on, shortened)", r == 1);
    expect("truncated to 11 chars", strlen(got) == 11);
    expect("terminated inside the buffer", rd8(DST + 11) == 0);
    expect("canary past buffer intact (no overflow)", rd8(DST + 12) == 0xAA);

    printf("malformed (field absent):\n");
    g_field[0] = NULL;
    memset(g_mem + DST, 0xCC, DST_CAP);
    r = call_guard(SITE_12, DST, 0, DST + 12);
    expect("result is failure", r == 0);
    expect("canary intact", rd8(DST + 12) == 0xAA);

    printf("uncatalogued site (growable string): unbounded reader runs:\n");
    g_field[0] = "a-very-long-value-into-a-big-buffer";
    memset(g_mem + DST, 0xCC, DST_CAP);
    r = call_guard(SITE_UNKNOWN, DST, 0, 0);
    guest_str(DST, got, sizeof got);
    expect("result is success", r == 1);
    expect("string copied whole (unchanged behaviour)",
           strcmp(got, "a-very-long-value-into-a-big-buffer") == 0);

    printf("the unguarded copy really overflows (the vulnerability):\n");
    /* The raw reader the guard stands in for: a 32-char field into the same
     * 12-byte buffer runs clean off the end and past the canary. */
    g_field[0] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; /* 32 chars */
    memset(g_mem + DST, 0xCC, DST_CAP);
    wr8(DST + 12, 0xAA);
    {
        X86 c;
        memset(&c, 0, sizeof c);
        uint32_t esp = STACK_TOP;
        esp -= 4; wr32(esp, 0);
        esp -= 4; wr32(esp, DST);
        esp -= 4; wr32(esp, SITE_12);
        c.r[R_ESP] = esp;
        c.r[R_ECX] = CTX;
        fn_004432d0(&c); /* the unbounded reader directly */
    }
    expect("canary overwritten by the raw copy", rd8(DST + 12) != 0xAA);

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
