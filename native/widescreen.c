/* native/widescreen.c - Heroes V's interface on screens wider (or taller) than
 * 4:3 (design notes ultrawide-plan.md).
 *
 * The interface is laid out on a virtual screen and mapped onto the real one
 * with separate x and y scales:
 *   screen = (virtual - origin) * (screen size / virtual size)
 * The globals (floats) belong to CVirtualScreenController:
 *   010d2cf4/cf8  the real screen (backbuffer) size
 *   010d2ce4/ce8  the virtual screen size every conversion reads
 *   010d2cec/cf0  the base virtual size, 1024x768; only 00e38100 reads it
 * SetScreenSize (00e38100) copies the base into the current virtual size, logs
 * "Screen resolution changed" and runs the console command reposition_interfaces.
 * In this exe the base never changes, so a 21:9 screen stretches 1024x768 to
 * it. Tribes of the East's copy of the function first widens the virtual screen
 * to the screen's aspect (768 high, or 1024 wide when taller than 4:3); the
 * replacement below does the same by setting the base before the original runs.
 *
 * Each root screen is then placed by 00e74f30 (WindowScreen, vtable slot): its
 * shared description's HorAlign (+0xfc) and VerAlign (+0x100) pick
 *   0 ALIGN_LOW_END  0 .. 1024          1 ALIGN_CENTER  (virtual - 1024)/2 ..
 *   2 ALIGN_HIGN_END virtual - 1024 ..  3 ALIGN_SCREEN  0 .. virtual
 * (768 vertically). An untagged screen gets ALIGN_CENTER across and
 * ALIGN_LOW_END down (00e6cdb0); the replacement centres it down too, for
 * screens taller than 4:3. The tags themselves come from Tribes of the East's
 * data through tools/widescreen_mod.py (UserMODs/Widescreen.h5u), which also
 * declares them in types.xml: the base game's schema never named them, so its
 * loader (00e575c0) skipped them.
 *
 * The base game only: Tribes of the East widens its virtual screen and reads
 * the tags itself. The 3D side (Hor+) is native/horplus.c.
 *
 * The player's interface bounds (edge-anchored windows kept in a narrower
 * box) are native/ui_bounds.c, shared with Tribes of the East; a phone's safe
 * area and its larger panels are native/phone_hud.c, shared by all three.
 *
 * RECOMP_HOMM5_WIDE_UI=0 keeps the original 1024x768 virtual screen;
 * RECOMP_TRACE_WIDE=1 logs both steps. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "native_seam.h"
#include "x86.h"

enum {
    BASE_VIRTUAL_W = 0x010d2cec, /* float */
    BASE_VIRTUAL_H = 0x010d2cf0,
    SCREEN_DESC = 0xc8,          /* WindowScreen: its shared description */
    DESC_HOR_ALIGN = 0xfc,
    DESC_VER_ALIGN = 0x100,
    ALIGN_LOW_END = 0,
    ALIGN_CENTER = 1,
    ALIGN_HIGH_END = 2,
    ALIGN_SCREEN = 3,
};

void fn_00e38100(X86 *c);
void fn_00e74f30(X86 *c);
/* native/phone_hud.c: a root against an edge, and its placement inside a
 * phone's safe area (it runs 00e74f30 itself). */
void homm5_hud_note_edge_root(uint32_t root, uint32_t desc);
void homm5_hud_place(X86 *c, uint32_t root);

/* Whether an alignment puts a root against the screen's edge: whole-screen
 * roots and those at the far end (the near end is centred, below). */
static int edge_align(uint32_t a) {
    return a == ALIGN_HIGH_END || a == ALIGN_SCREEN;
}

static int env_flag(const char *name, int fallback) {
    const char *v = getenv(name);
    return v && *v ? v[0] != '0' : fallback;
}

static int wide_ui(void) {
    static int on = -1;
    if (on < 0)
        on = env_flag("RECOMP_HOMM5_WIDE_UI", 1);
    return on;
}

static int trace_wide(void) {
    static int on = -1;
    if (on < 0)
        on = env_flag("RECOMP_TRACE_WIDE", 0);
    return on;
}

/* 00e38100: CVirtualScreenController::SetScreenSize (fastcall: ECX width,
 * EDX height, both ints). */
void homm5_set_screen_size(X86 *c) {
    const int w = (int)c->r[R_ECX], h = (int)c->r[R_EDX];
    if (wide_ui() && w > 0 && h > 0) {
        /* Tribes of the East's rule, rounding included (00e31fa0 there). */
        const float aspect = (float)w / (float)h;
        float vw = 1024.0f, vh = 768.0f;
        if (aspect > 4.0f / 3.0f)
            vw = (float)(int)(768.0f * aspect + 0.99f);
        else
            vh = (float)(int)(1024.0f / aspect + 0.99f);
        wrf32(BASE_VIRTUAL_W, vw);
        wrf32(BASE_VIRTUAL_H, vh);
        if (trace_wide())
            fprintf(stderr, "[wide] screen %dx%d: virtual screen %.0fx%.0f\n", w, h, vw, vh);
    }
    fn_00e38100(c);
}

/* 00e74f30: a root WindowScreen places itself on the virtual screen
 * (thiscall: ECX the window; no stack arguments). */
void homm5_screen_place(X86 *c) {
    const uint32_t root = c->r[R_ECX];
    const uint32_t desc = rd32(root + SCREEN_DESC);
    if (!wide_ui() || !desc) {
        fn_00e74f30(c);
        return;
    }
    const uint32_t hor = rd32(desc + DESC_HOR_ALIGN), ver = rd32(desc + DESC_VER_ALIGN);
    if (edge_align(hor) || edge_align(ver))
        homm5_hud_note_edge_root(root, desc);
    /* The description is shared by every instance of the screen: centre it for
     * this call only, and leave the data as loaded. */
    if (hor == ALIGN_LOW_END)
        wr32(desc + DESC_HOR_ALIGN, ALIGN_CENTER);
    if (ver == ALIGN_LOW_END)
        wr32(desc + DESC_VER_ALIGN, ALIGN_CENTER);
    if (trace_wide())
        fprintf(stderr, "[wide] screen %08x (desc %08x) align %u,%u\n", root, desc, hor, ver);
    homm5_hud_place(c, root); /* inside the safe area, and larger for a phone's map or battle */
    wr32(desc + DESC_HOR_ALIGN, hor);
    wr32(desc + DESC_VER_ALIGN, ver);
}
