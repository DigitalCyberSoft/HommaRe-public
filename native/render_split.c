/* render_split.c - where a frame's 3D world ends and its interface begins.
 *
 * The host draws a touch device's 3D at the render quality's share of the
 * screen and upscales it with MetalFX; told when the world of the frame is
 * drawn (host_render_scene_end), it draws the interface after it at the
 * screen's own resolution (kit/host/gpu/metal/d3d9_metal.mm scene_end).
 *
 * A screen's Draw runs its world pass (007c2960 in the base game), which
 * draws the 3D through CGameView::Draw (0062b9e0, IGameView slot 0x50) for
 * the main view, a second view and an alternate one; world labels follow,
 * then the interface pass (007c5be0), whose own 3D scene goes through the
 * same function and belongs to the interface. Overlay screens draw the
 * screen under them first, so the world comes before every interface. A
 * battle's frame with a second world view marks the first; the rest draws at
 * the full size. */
#include <stdint.h>

#include "native_seam.h"
#include "x86.h"

#ifdef RECOMP_OVERRIDE_HEADER
#include RECOMP_OVERRIDE_HEADER
#endif
#ifdef RECOMP_ADDRESS_HEADER
#include RECOMP_ADDRESS_HEADER
#endif

/* The base game's: CGameView::Draw and where the world pass gets back from
 * it (an expansion's addresses.h names its own). */
#ifndef HOMM5_FN_GAME_VIEW_DRAW
#define HOMM5_FN_GAME_VIEW_DRAW fn_0062b9e0
#define HOMM5_RET_VIEW_MAIN 0x007c2eceu   /* the main view */
#define HOMM5_RET_VIEW_SECOND 0x007c2f02u /* a second view */
#define HOMM5_RET_VIEW_ALT 0x007c2befu    /* the alternate view */
#endif

void HOMM5_FN_GAME_VIEW_DRAW(X86 *c);

/* CGameView::Draw (thiscall, no stack arguments). */
void homm5_game_view_draw(X86 *c) {
    const uint32_t ret = rd32(c->r[R_ESP]);
    HOMM5_FN_GAME_VIEW_DRAW(c);
    if (ret == HOMM5_RET_VIEW_MAIN || ret == HOMM5_RET_VIEW_SECOND || ret == HOMM5_RET_VIEW_ALT)
        host_render_scene_end();
}
