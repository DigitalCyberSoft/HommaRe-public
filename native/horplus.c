/* native/horplus.c - Hor+ for Heroes V's 3D on screens wider than 4:3
 * (design notes), shared by the base game and Tribes of the East.
 * Addresses come from the game's RECOMP_OVERRIDE_HEADER (games/<id>/native/
 * overrides.h), the base game's being the defaults, as in native/touch.c; the
 * header also redirects each FN_<addr> below to its replacement here.
 *
 * The engine's projection (given h/w and a field of view in degrees) keeps
 * the horizontal angle on anything wider than 4:3, so a 21:9 screen shows only
 * 4:3/21:9 of the height a 4:3 one does: the map and the battle look zoomed
 * in, their top and bottom cut off. Taller than 4:3 it keeps the vertical
 * angle of a 4:3 screen instead. The replacements give wide screens that same
 * rule: the vertical angle a 4:3 screen of this field of view has, and more to
 * the sides. That is the field of view
 *   2 atan(tan(fov / 2) * 0.75 / (h / w))
 * in the engine's own wide-screen formula, so each function that turns a field
 * of view into a picture or a ray gets it at entry: the projection, the
 * screen -> ray pick and the world -> screen projection. The engine's own
 * conversion to exactly this angle, which a few cameras apply first (town
 * scenes and cutscenes with their arena's flag, some adventure-screen views),
 * passes the angle through under Hor+, or those cameras would be widened twice.
 * native/camera.c follows the same rule for the touch camera's ground pick.
 * Only while a map or battle is open (in_game); RECOMP_HOMM5_HORPLUS=0 keeps
 * the original projection everywhere, RECOMP_TRACE_WIDE=1 logs when it starts. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "x86.h"

#ifdef RECOMP_OVERRIDE_HEADER
#include RECOMP_OVERRIDE_HEADER
#endif
#ifdef RECOMP_ADDRESS_HEADER
#include RECOMP_ADDRESS_HEADER
#endif
#ifndef HOMM5_SCREEN_STACK
#define HOMM5_SCREEN_STACK 0x0114b2e4
#define HOMM5_VT_ADVENTURE 0x00fab2ac
#define HOMM5_VT_COMBAT 0x00fa79c8
#endif
#ifndef HOMM5_FN_PROJECTION
#define HOMM5_FN_PROJECTION fn_00a1a1e0
#define HOMM5_FN_SCREEN_RAY fn_00a18c90
#define HOMM5_FN_WORLD_TO_SCREEN fn_00a19030
#define HOMM5_FN_FOV_FOR_SCREEN fn_00a19940
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
    SCREEN_STACK = HOMM5_SCREEN_STACK, /* std::list<IScreen*>: the sentinel node */
    VT_ADVENTURE = HOMM5_VT_ADVENTURE, /* IScreen subobject vtables (native/touch.c) */
    VT_COMBAT = HOMM5_VT_COMBAT,
};

/* The translated originals. Only the projection is required: Hammers of
 * Fate has no wide-screen angle function, and the field-of-view pick
 * functions below have no callers in any of the three exes (the live pick,
 * base 00a18e60 / 00a191e0, inverts the matrix the projection built, so it
 * follows Hor+ by itself); their hooks stay for the base game and Tribes of
 * the East as harmless spares until they are removed. */
void HOMM5_FN_PROJECTION(X86 *c);
#ifdef HOMM5_FN_SCREEN_RAY
void HOMM5_FN_SCREEN_RAY(X86 *c);
#endif
#ifdef HOMM5_FN_WORLD_TO_SCREEN
void HOMM5_FN_WORLD_TO_SCREEN(X86 *c);
#endif
#ifdef HOMM5_FN_FOV_FOR_SCREEN
void HOMM5_FN_FOV_FOR_SCREEN(X86 *c);
#endif

static int env_flag(const char *name, int fallback) {
    const char *v = getenv(name);
    return v && *v ? v[0] != '0' : fallback;
}

static int trace_horplus(void) {
    static int on = -1;
    if (on < 0)
        on = env_flag("RECOMP_TRACE_WIDE", 0);
    return on;
}

/* 0 off, 1 in a game (map or battle), 2 everywhere (to try a menu's scene). */
static int horplus_enabled(void) {
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_HOMM5_HORPLUS");
        on = !v || !*v ? 1 : v[0] == '0' ? 0 : v[0] == '2' ? 2 : 1;
    }
    return on;
}

/* Whether the adventure map or a battle is open (anywhere in the screen
 * stack, so a dialog or the town over the map keeps the map's projection).
 * The main menu's scene and the screens outside a game are built for 4:3:
 * a wider view shows past their backdrop, so they keep the original crop. */
static int in_game(void) {
    const uint32_t sentinel = rd32(SCREEN_STACK);
    if (!sentinel)
        return 0;
    int guard = 64;
    for (uint32_t node = rd32(sentinel); node && node != sentinel && guard--; node = rd32(node)) {
        const uint32_t screen = rd32(node + 8u);
        const uint32_t vt = screen ? rd32(screen) : 0;
        if (vt == VT_ADVENTURE || vt == VT_COMBAT)
            return 1;
    }
    return 0;
}

int homm5_horplus(void) {
    const int mode = horplus_enabled();
    if (!mode || (mode == 1 && !in_game()))
        return 0;
    static int told = -1;
    if (trace_horplus() && told != 1) {
        told = 1;
        fprintf(stderr, "[wide] Hor+ on (a map or battle is open)\n");
    }
    return 1;
}

static void widen_fov(uint32_t at, float h_over_w) {
    if (!(h_over_w > 0.0f && h_over_w < 0.75f))
        return;
    const double t = tan(rdf32(at) * (M_PI / 360.0));
    wrf32(at, (float)(atan(t * 0.75 / h_over_w) * (360.0 / M_PI)));
}

/* The projection (base 00a1a1e0; thiscall: h/w, fov, znear, zfar, offset) */
void homm5_projection(X86 *c) {
    if (homm5_horplus())
        widen_fov(c->r[R_ESP] + 8u, rdf32(c->r[R_ESP] + 4u));
    HOMM5_FN_PROJECTION(c);
}

#ifdef HOMM5_FN_SCREEN_RAY
/* Screen point -> ray (base 00a18c90; fastcall ECX, EDX out; matrix, screen size w,h, point, fov) */
void homm5_screen_ray(X86 *c) {
    if (homm5_horplus()) {
        const uint32_t size = rd32(c->r[R_ESP] + 8u);
        const float w = rdf32(size), h = rdf32(size + 4u);
        if (w > 0.0f)
            widen_fov(c->r[R_ESP] + 16u, h / w);
    }
    HOMM5_FN_SCREEN_RAY(c);
}
#endif

#ifdef HOMM5_FN_WORLD_TO_SCREEN
/* World point -> screen (base 00a19030; thiscall ECX the point: screen size w,h, fov, out) */
void homm5_world_to_screen(X86 *c) {
    if (homm5_horplus()) {
        const uint32_t size = rd32(c->r[R_ESP] + 4u);
        const float w = rdf32(size), h = rdf32(size + 4u);
        if (w > 0.0f)
            widen_fov(c->r[R_ESP] + 8u, h / w);
    }
    HOMM5_FN_WORLD_TO_SCREEN(c);
}
#endif

#ifdef HOMM5_FN_FOV_FOR_SCREEN
/* The engine's wide-screen angle (base 00a19940; thiscall ECX the screen size: fov; returns the angle in ST0, RET 4) */
void homm5_fov_for_screen(X86 *c) {
    if (!homm5_horplus()) {
        HOMM5_FN_FOV_FOR_SCREEN(c);
        return;
    }
    const uint32_t esp = c->r[R_ESP];
    fpush(c, (double)rdf32(esp + 4u));
    c->eip = rd32(esp);
    c->r[R_ESP] = esp + 8u;
    recomp_return(c);
}
#endif
