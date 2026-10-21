/* native/hof_paks.c - the wide-screen pak over Hammers of Fate's own data.
 * Hammers of Fate only.
 *
 * The game mounts data\*.pak with priority 0 (009ee5fc), then a table of
 * named paks over them (built at 006ecc5c..006ed1f2: p0..p3-data 1, a1-data
 * 2, a1p1-data 3, the texts -1), and the file system takes the highest
 * priority's copy of a file (the newer one at equal priority). The kit's
 * overlay lists the profile's data/zz-*.pak among data\*.pak, so a file
 * dataa1 also carries would stay the expansion's. 009eded0 mounts one pak
 * (thiscall: the path as a string whose first word points at its
 * characters, then the priority): a zz-*.pak gets OVERLAY_PRIORITY, above
 * every pak of the game's. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "x86.h"

enum { OVERLAY_PRIORITY = 100 };

void fn_009eded0(X86 *c);

static int lower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

void hof_mount_pak(X86 *c) {
    const uint32_t esp = c->r[R_ESP];
    const uint32_t text = rd32(rd32(esp + 4u));
    uint32_t name = text;
    for (uint32_t i = text; i && rd8(i) && i - text < 1024u; ++i)
        if (rd8(i) == '\\' || rd8(i) == '/')
            name = i + 1u;
    if (name && lower(rd8(name)) == 'z' && lower(rd8(name + 1u)) == 'z' && rd8(name + 2u) == '-') {
        if (getenv("RECOMP_TRACE_WIDE")) {
            char path[256];
            uint32_t n = 0;
            while (n + 1 < sizeof path && rd8(text + n))
                path[n] = (char)rd8(text + n), ++n;
            path[n] = 0;
            fprintf(stderr, "[wide] %s mounts with priority %d (was %d)\n", path,
                    OVERLAY_PRIORITY, (int32_t)rd32(esp + 8u));
        }
        wr32(esp + 8u, OVERLAY_PRIORITY);
    }
    fn_009eded0(c);
}
