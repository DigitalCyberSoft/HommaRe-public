/* games/toe/native/addresses.h - Tribes of the East's addresses for the
 * shared native code (native/touch.c, save.cpp, camera.c, horplus.c, ui_bounds.c),
 * which the build passes it to as RECOMP_ADDRESS_HEADER (game.toml
 * [translate] native addresses); the translated chunks never include it, so
 * changing one only rebuilds those sources. Found with games/map_addresses.py
 * from the base game's (native/touch.c): functions and data through
 * byte-identical functions, vtables through RTTI. */
#pragma once

#define HOMM5_CURSOR_OBJECT 0x0110c470 /* NUI::CCursor */
#define HOMM5_CURSOR_VTABLE 0x00f56878
#define HOMM5_CURSOR_X 0x0110c464
#define HOMM5_CURSOR_Y 0x0110c468
#define HOMM5_CURSOR_ID 0x0110c4b8
/* CCursor::Draw and its hardware-cursor flag (getter 0060e0c0). */
#define HOMM5_FN_CURSOR_DRAW fn_0060da80
#define HOMM5_CURSOR_HW 0x0110c450
#define HOMM5_HOVER_TIMEOUT 0x0108e21c /* Options.Tooltips.HoverTimeout, 1.0 */
#define HOMM5_SCREEN_STACK 0x0110b6ec
/* CAdventureScreen grew by 0x90 and CCombatArenaScreen shrank by 0xf4 since
 * the base game; their IScreen subobjects keep their place in the class. */
#define HOMM5_VT_ADVENTURE 0x00f5d0a4
#define HOMM5_VT_COMBAT 0x00f592a4
#define HOMM5_SUB_ADVENTURE 0x844
#define HOMM5_SUB_COMBAT 0x564
#define HOMM5_SCREEN_WINDOWS 0xa0
#define HOMM5_WINDOWS_MODAL 0x1f4
#define HOMM5_SAVE_SETTINGS 0x005c5830 /* the base game's 006bfe80 */

/* The adventure map's camera (native/camera.c): what CCameraOperator::Update
 * (006fd1e0, the base game's 007f26b0) reaches, mapped as above; the screen
 * fields from the adventure screen's init. */
#define HOMM5_CAM_VTABLE 0x00f62010       /* CCamera */
#define HOMM5_CAM_OPERATOR_VTABLE 0x00f62450 /* CCameraOperator */
#define HOMM5_SCREEN_CAMERA 0xa4
#define HOMM5_SCREEN_OPERATOR 0x5e4
#define HOMM5_VIRTUAL_SCREEN 0x01093780 /* the UI's virtual screen size, floats */
#define HOMM5_CAMERA_UPDATE_ORIG fn_006fd1e0

/* The battle camera's start (native/camera.c): CArenaCameraConfigurator
 * (vtable 0xf61ea0) slot 8, found through its RTTI. */
#define HOMM5_BATTLE_DEFAULTS_ORIG fn_006f39d0
/* The battle screen's camera operator (native/camera.c zoom_view_camera): the
 * battle's camera setup 0064c0d0 repeats the base game's 0074bcd0 call for
 * call and keeps the operator, made by CCameraOperator's constructor 006fb760
 * (found by its vtable 0xf62450), here; the base game's +0x4e4. */
#define HOMM5_COMBAT_OPERATOR 0x3b8

/* Hor+ (native/horplus.c): the base game's 00a1a1e0 (projection), 00a18c90
 * (screen -> ray), 00a19030 (world -> screen) and 00a19940 (the engine's
 * wide-screen angle), byte-identical here. Tribes of the East widens the
 * interface's virtual screen and anchors its screens itself (SetScreenSize
 * 00e31fa0, data in a2p1-data.pak), so it needs only these. */
#define HOMM5_FN_PROJECTION fn_00966970
#define HOMM5_FN_SCREEN_RAY fn_00965420
#define HOMM5_FN_WORLD_TO_SCREEN fn_009657c0
#define HOMM5_FN_FOV_FOR_SCREEN fn_009660d0

/* Interface bounds (native/ui_bounds.c): the base game's axis 00e43650 is
 * 00e3d540 here, asked for x from SetParentRect 00e35620 at 00e3572e; the
 * current virtual screen is 01093780 (00e31fa0 widens it). */
#define HOMM5_VIRTUAL_W 0x01093780
#define HOMM5_FN_ANCHOR_AXIS fn_00e3d540
#define HOMM5_RET_ANCHOR_X 0x00e35733
#define HOMM5_RET_ANCHOR_Y 0x00e3577d /* and for y (the dragged icon, native/ui_bounds.c; the phone rows, native/phone_layout.c) */

/* The phone's interface (native/phone_hud.c), from the base game's through
 * byte-identical functions, RTTI and the classes' sub-object offsets
 * (research/phone-hud-expansions-addresses.md, 2026-10-19). The root
 * placement 00e734a0 is the base game's 00e74f30 byte for byte, the alignment
 * fields of its description included. "probable": the town's, hire, build,
 * hero and loading screens' IScreen vtables and offsets (by structure), the
 * clipped draw 00e3f9c0, the loading screen's constructor 0069ebe0 and the
 * tooltip timer 00e9b230; the rest hash-identical. */
#define HOMM5_VT_TOWN 0x00f734d4 /* NUI::CTownScreen's IScreen, at +0x4c0 */
#define HOMM5_SUB_TOWN 0x4c0
#define HOMM5_VT_LOADING 0x00f5c7d8 /* NUI::CPWLScreenInterface's, at +0x1e4 */
#define HOMM5_SUB_LOADING 0x1e4
#define HOMM5_VT_HIRE 0x00f72c10 /* NUI::CHireScreen's, at +0x290 */
#define HOMM5_SUB_HIRE 0x290
#define HOMM5_VT_BUILD 0x00f74fe4 /* NUI::CTownBuildScreen's, at +0x26c */
#define HOMM5_SUB_BUILD 0x26c
#define HOMM5_VT_HERO 0x00f665c0 /* NUI::CHeroScreen2's, at +0x844 */
#define HOMM5_SUB_HERO 0x844
#define HOMM5_VT_LOADING_MAIN 0x00f5c770 /* NUI::CPWLScreenInterface */
#define HOMM5_VT_MS_BUTTON 0x00fec640
#define HOMM5_VT_EDIT_LINE 0x00fed64c
#define HOMM5_VT_EDIT_BOX 0x00ff6010
#define HOMM5_VT_LIST 0x00fecbcc
#define HOMM5_VT_SCROLLER 0x00ff15bc
#define HOMM5_VT_TEXT_VIEW 0x00ff2004
#define HOMM5_UI_VIRTUAL_W 0x01093780
#define HOMM5_UI_VIRTUAL_H 0x01093784
#define HOMM5_UI_REAL_W 0x01093790
#define HOMM5_UI_REAL_H 0x01093794
#define HOMM5_UI_ORIGIN_X 0x0121ae48
#define HOMM5_UI_ORIGIN_Y 0x0121ae4c
#define HOMM5_FN_WINDOW_DRAW fn_00e37c10
#define HOMM5_FN_WINDOW_CONTAINS fn_00e35c30
#define HOMM5_FN_SIMPLE_DRAW fn_00e3f9c0
#define HOMM5_FN_TEXT_LAYOUT fn_00e3f2e0
#define HOMM5_ADDR_TEXT_LAYOUT 0x00e3f2e0u
#define HOMM5_FN_LOADING_SCREEN fn_0069ebe0
#define HOMM5_FN_SCREEN_MOUSE_0 fn_00e368e0
#define HOMM5_FN_SCREEN_MOUSE_1 fn_00e369a0
#define HOMM5_FN_SCREEN_MOUSE_2 fn_00e36a30
#define HOMM5_FN_SCREEN_MOUSE_3 fn_00e36af0
#define HOMM5_FN_SCREEN_MOUSE_4 fn_00e36bb0
#define HOMM5_FN_SCREEN_MOUSE_5 fn_00e36c40
#define HOMM5_FN_SCREEN_MOUSE_6 fn_00e36d00
#define HOMM5_FN_TOOLTIP_TIMER fn_00e9b230
#define HOMM5_FN_TOOLTIP_MOVE fn_00e9b170
/* The game aligns its roots itself (its data's HorAlign/VerAlign tags):
 * native/phone_hud.c's own placement hook only moves them into the safe area. */
#define HOMM5_FN_SCREEN_PLACE fn_00e734a0
#define HOMM5_ADDR_SCREEN_PLACE 0x00e734a0u
#define HOMM5_HUD_PLACE_HOOK 1
/* CSkeletonAnimator's NeedUpdate, the translated original (base 0067c060). */
#define HOMM5_ANIM_NEED_UPDATE_ORIG fn_005640e0

/* granny2.dll (this game's own build of it): the quadratic B-spline
 * quaternion sample 50011890 and the float constants it reads
 * (native/granny.c). */
void fn_50011890(X86 *c);
#define GRANNY_SPLINE_QUAT_ORIG fn_50011890
#define GRANNY_SPLINE_ONE 0x5004c0d0u
#define GRANNY_SPLINE_NORM_A 0x5004c478u
#define GRANNY_SPLINE_NORM_B 0x5004c1d0u
/* granny2.dll 500127b0: the knot interval search before each curve sample. */
void fn_500127b0(X86 *c);
#define GRANNY_KNOT_SEARCH_ORIG fn_500127b0
/* granny2.dll 50028950: one weighted local transform into the blend
 * accumulator, and the orientation threshold it compares with. */
void fn_50028950(X86 *c);
#define GRANNY_POSE_ACCUMULATE_ORIG fn_50028950
#define GRANNY_POSE_DOT_MIN 0x5004c0ccu
/* granny2.dll 50028b90: finish the blended local pose of a run of bones, and
 * the constants of its orientation normalisation. */
void fn_50028b90(X86 *c);
#define GRANNY_POSE_FINISH_ORIG fn_50028b90
#define GRANNY_NORM_HI 0x5004c6d8u
#define GRANNY_NORM_LO 0x5004c6d4u
#define GRANNY_NORM_POLY_A 0x5004c6d0u
#define GRANNY_NORM_POLY_B 0x5004c6ccu
/* The MMX vertex normal skinning (native/lighting.c): 00557470 and its reciprocal
 * square root table. */
void fn_00557470(X86 *c);
#define HOMM5_LIGHT_VERTEX_ORIG fn_00557470
#define HOMM5_LIGHT_RSQRT 0x010f94b0u
/* Skinned parts drawn on the GPU (native/gpu_skin.cpp): the part fill
 * 00556660, the staged copy 005559c0 and the big vertex buffer's allocation
 * 00500400 (the base game's 0066e4d0, 0066d950 and 00606af0), 0068fc20's
 * alpha (00587570) and its two global vectors (01138ee8 / 01138ef4 there),
 * the reciprocal square root and cube-root tables (01139088 / 01142088). */
#define H5SKIN_FILL_ORIG fn_00556660
#define H5SKIN_COPY_ORIG fn_005559c0
#define H5SKIN_ALLOC_ORIG fn_00500400
#define H5SKIN_ALPHA_FN 0x00587570u
#define H5SKIN_ALPHA_A 0x010f9300u
#define H5SKIN_ALPHA_B 0x010f930cu
#define H5SKIN_RSQRT 0x010f94b0u
#define H5SKIN_CBRT 0x011024b0u
#define H5SKIN_LOW_RAM 0x0108d1e8u /* gfx_low_ram's byte (the base game's 010cd1f9) */
#define H5SKIN_POSITIONS_ORIG fn_005578e0 /* the bounds' SSE position skinning (0066f600) */


/* native/overrides.c: the x87 float max/min helpers, byte for byte the base
 * game's 00614540 / 00614570; the base game's box grow 0066e070 has no
 * counterpart here (HOMM5_AABB_ADD_ORIG left undefined). */
#define HOMM5_FLOAT_MAX_ORIG fn_0050e5b0
#define HOMM5_FLOAT_MIN_ORIG fn_0050e5e0

/* native/scene.c: the 32-bit index buffer's fill (the base game's 0060d260)
 * and its "no base" flag (0x011375e8 there). */
#define HOMM5_IB_FILL_ORIG fn_00507290
#define HOMM5_IB_NOBASE 0x010f7898u

/* native/scene.c: the cull tests (the base game's 00a1a680, 00a1a9f0, 00a1ab50)
 * and the sign mask they XOR with (four 0x80000000). */
#define HOMM5_CULL_TEST_ORIG fn_00966e10
#define HOMM5_CULL_PUSH_ORIG fn_00967180
#define HOMM5_CULL_OBJECT_ORIG fn_009672e0
#define HOMM5_CULL_SIGNMASK 0x00f4a240u

/* native/scene.c: a vertex pool's range (the base game's 0060d810). */
#define HOMM5_POOL_ALLOC_ORIG fn_00507840

/* The end of a frame's 3D world (native/render_split.c): CGameView::Draw
 * (the NGScene::CGameView vtable's slot 0x50) and where the world pass gets
 * back from it - its main, second and alternate view. */
#define HOMM5_FN_GAME_VIEW_DRAW fn_00519620
#define HOMM5_RET_VIEW_MAIN 0x006cc05eu
#define HOMM5_RET_VIEW_SECOND 0x006cc08au
#define HOMM5_RET_VIEW_ALT 0x006cbd7fu
