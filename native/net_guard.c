/* native/net_guard.c - bound the game's network string copies against a
 * hostile ubi-gs server or peer (README "Playing online safely").
 *
 * The GS protocol parser (GameSpy clDataList) reads one list element into a
 * caller's buffer with an unbounded byte copy: the base game's FUN_004432d0,
 *   do { c = *src++; *dst++ = c; } while (c != 0);
 * with no length from the message. The player-info deserializer (0042a5e0,
 * reached from the PLAYERINFO message handler 0042a990) copies seven server
 * strings this way into stack buffers of 12/16/36/36/68/132/132 bytes, so a
 * 12-character first field overflows the first buffer and smashes the frame
 * (past the stack cookie). ubi-gs caps these fields server-side now, but a
 * malicious server, or a peer that speaks the protocol, bypasses that.
 *
 * Beside the unbounded reader the game has a BOUNDED one (00443390): the same
 * list extraction, then strncpy(dst, src, size) with a forced terminator.
 * This replacement stands in for the unbounded reader and, for every read
 * whose destination size we know, forwards to the bounded one with that size.
 * An over-long field is truncated and the read reports failure, which the
 * deserializers already treat as a malformed message and drop - the game's
 * own handling of a bad message. A read we have not catalogued (the ones into
 * heap std::strings that grow to fit) runs the original copy unchanged.
 *
 * A read's site is told apart by its return address on the guest stack: every
 * CALL to the reader has its own. The sizes are the deserializers' buffers,
 * read from the listings and the translation; they are the same in all three
 * games (the three executables are one code base). games/<id>/native/
 * addresses.h gives the expansion's reader and call-site addresses; the base
 * game's are below.
 *
 * RECOMP_HOMM5_NET_GUARD=0 turns it off for a run (the raw copy, to reproduce
 * the overflow); RECOMP_TRACE_NET=1 logs every truncation, not just the first
 * per site. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "x86.h"

/* RECOMP_OVERRIDE_HEADER declares the replacement defined here;
 * RECOMP_ADDRESS_HEADER carries an expansion's addresses (the base game's are
 * the defaults below). The idiom is native/touch.c's. */
#ifdef RECOMP_OVERRIDE_HEADER
#include RECOMP_OVERRIDE_HEADER
#endif
#ifdef RECOMP_ADDRESS_HEADER
#include RECOMP_ADDRESS_HEADER
#endif

/* The game's two clDataList string readers: the unbounded one this file
 * stands in for (its translated body, still emitted under its lower-case
 * name), and the bounded one it forwards to. Both are __thiscall with the
 * list in ECX; the unbounded takes (dst, index) and returns on RET 0x8, the
 * bounded (dst, index, size) on RET 0xc. */
#ifndef HOMM5_GS_STR_ORIG
#define HOMM5_GS_STR_ORIG fn_004432d0
#define HOMM5_GS_STR_BOUNDED fn_00443390
#endif

/* The catalogued reads: a call site's return address and the byte size of the
 * fixed buffer it copies into. An expansion defines its own list in
 * addresses.h. The base game's are every server-to-client deserializer that
 * copies a field into a fixed buffer (the message named by the type it parses):
 *   PLAYERINFO 0042a5e0             12/16/36/36/68/132/132
 *   GROUP_INFO member loop 004242d0 36/132/132
 *   MEMBER_JOIN 00424f00            36/132/132
 *   proxy JOINWAITMODULE 0043f450   16/132
 *   PROXY_HANDLER sub 1 00427190    36/36/132 (the module's proxy list)
 *   GAME_STARTED 004237e0           132/132 (host ip, alt ip)
 *   LOBBY_MSG sub 3 00422230        132
 *   UPDATEFRIEND 00428c60           132 (optional trailing string; its own
 *                                        absent-field fallback writes 132)
 *   persistantdata reply 004303d0   36
 *   id readers 00425f20 / 00423390 (also entered as 00425790) / 00425940
 *     / 004260b0                    16/16/36/16
 * (The room-info reader 0041fd00 and the NAT replies already read bounded or
 * typed.) The other deserializers that reach the reader copy into growable
 * std::strings, which this guard leaves alone (see the inventory in the
 * commit). */
#ifndef HOMM5_GS_STR_SITES
#define HOMM5_GS_STR_SITES                                                                         \
    {0x0042a6a8u, 12}, {0x0042a6bcu, 16}, {0x0042a6d0u, 36}, {0x0042a6e4u, 36}, {0x0042a6f8u, 68}, \
        {0x0042a70cu, 132}, {0x0042a720u, 132}, {0x00424a3cu, 36}, {0x00424a7fu, 132},             \
        {0x00424a9au, 132}, {0x00425020u, 36}, {0x0042505fu, 132}, {0x00425076u, 132},             \
        {0x0043f538u, 16}, {0x0043f54eu, 132}, {0x004272fau, 36}, {0x00427374u, 36},               \
        {0x004273ffu, 132}, {0x004238feu, 132}, {0x00423936u, 132}, {0x0042237au, 132},            \
        {0x00428d6fu, 132}, {0x004304f4u, 36}, {0x00425fcau, 16}, {0x004234a6u, 16},               \
        {0x00425a70u, 36}, {0x00426187u, 16},
#endif

void HOMM5_GS_STR_ORIG(X86 *c);
void HOMM5_GS_STR_BOUNDED(X86 *c);

struct gs_site {
    uint32_t ret;  /* the return address that identifies the call site */
    uint16_t size; /* the destination buffer's size in bytes */
};
static const struct gs_site gs_sites[] = {HOMM5_GS_STR_SITES};
/* One "already logged" flag per site, so a hostile peer shows in the log once
 * without flooding it. */
static uint8_t gs_logged[sizeof gs_sites / sizeof gs_sites[0]];

static int net_guard_on(void) {
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_HOMM5_NET_GUARD");
        on = v && *v ? v[0] != '0' : 1;
    }
    return on;
}

static int trace_net(void) {
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_TRACE_NET");
        on = v && *v ? v[0] != '0' : 0;
    }
    return on;
}

/* The bounded reader forwards a server string into a fixed buffer, truncating
 * an over-long one instead of running off the end of the stack frame. */
void homm5_gs_str_read(X86 *c) {
    const uint32_t esp = c->r[R_ESP];
    const uint32_t ret = rd32(esp);
    const struct gs_site *site = NULL;
    size_t site_i = 0;

    if (net_guard_on()) {
        for (size_t i = 0; i < sizeof gs_sites / sizeof gs_sites[0]; ++i)
            if (gs_sites[i].ret == ret) {
                site = &gs_sites[i];
                site_i = i;
                break;
            }
    }
    if (!site) {
        /* A read we have not catalogued (a growable heap string): unchanged. */
        HOMM5_GS_STR_ORIG(c);
        return;
    }

    /* Forward to the bounded reader. Our frame is [ret, dst, index]; it wants
     * [ret, dst, index, size], so lay that one dword lower: the bounded
     * reader's RET 0xc then lands exactly where the unbounded reader's RET 0x8
     * would (both leave ESP at entry + 12 and jump to ret). The list stays in
     * ECX for both. */
    const uint32_t dst = rd32(esp + 4);
    const uint32_t index = rd32(esp + 8);
    const uint32_t nsp = esp - 4;
    wr32(nsp + 0x0, ret);
    wr32(nsp + 0x4, dst);
    wr32(nsp + 0x8, index);
    wr32(nsp + 0xc, site->size);
    c->r[R_ESP] = nsp;
    c->ret_expect = ret;
    HOMM5_GS_STR_BOUNDED(c);

    /* The bounded reader returns 0 when the field did not fit (truncated) or
     * was absent; either way the deserializer drops the message, which is how
     * the game answers a malformed one. Normal play keeps every field short
     * and present, so a drop here is a hostile or broken message: log the
     * first per site (or every one under RECOMP_TRACE_NET). */
    if (c->r[R_EAX] == 0 && (trace_net() || !gs_logged[site_i])) {
        gs_logged[site_i] = 1;
        fprintf(stderr, "[net_guard] GS string at %08x capped to %u bytes; message dropped\n", ret,
                site->size);
    }
}
