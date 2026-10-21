/* games/hof/native/addresses.h - Hammers of Fate's addresses for the shared
 * native code (native/touch.c, save.cpp, camera.c, horplus.c, ui_bounds.c), which the
 * build passes it to as RECOMP_ADDRESS_HEADER (game.toml [translate] native
 * addresses); the translated chunks never include it, so changing one only
 * rebuilds those sources. Found with games/map_addresses.py from the base
 * game's (native/touch.c): functions and data through byte-identical
 * functions, vtables through RTTI. */
#pragma once

#define HOMM5_CURSOR_OBJECT 0x0114bbe0 /* NUI::CCursor */
#define HOMM5_CURSOR_VTABLE 0x00fa89e4
#define HOMM5_CURSOR_X 0x0114bbd4
#define HOMM5_CURSOR_Y 0x0114bbd8
#define HOMM5_CURSOR_ID 0x0114bc28
/* CCursor::Draw and its hardware-cursor flag: the getter is vtable slot 10
 * here (00727c80), one later than the base game's. */
#define HOMM5_FN_CURSOR_DRAW fn_00727740
#define HOMM5_CURSOR_HW 0x0114bbc0
#define HOMM5_HOVER_TIMEOUT 0x010cff5c /* Options.Tooltips.HoverTimeout, 1.0 */
#define HOMM5_SCREEN_STACK 0x0114ab38
/* The IScreen subobjects sit earlier in the 2.1 screens' layouts. */
#define HOMM5_VT_ADVENTURE 0x00fb0c94
#define HOMM5_VT_COMBAT 0x00fad674
#define HOMM5_SUB_ADVENTURE 0x778
#define HOMM5_SUB_COMBAT 0x53c
#define HOMM5_SCREEN_WINDOWS 0x98 /* checked: the adventure init reads it where the base reads 0xa0 */
#define HOMM5_WINDOWS_MODAL 0x1f4
#define HOMM5_SAVE_SETTINGS 0x006f27b0 /* the base game's 006bfe80 */

/* The adventure map's camera (native/camera.c): what CCameraOperator::Update
 * (007df8b0, the base game's 007f26b0) reaches, mapped as above; the screen
 * fields from the adventure screen's init. */
#define HOMM5_CAM_VTABLE 0x00fb4a50       /* CCamera */
#define HOMM5_CAM_OPERATOR_VTABLE 0x00fb4e88 /* CCameraOperator */
#define HOMM5_SCREEN_CAMERA 0x9c
#define HOMM5_SCREEN_OPERATOR 0x568
#define HOMM5_VIRTUAL_SCREEN 0x010d4e24 /* the UI's virtual screen size, floats: read by GetVirtualSize 00e437a0 */
#define HOMM5_CAMERA_UPDATE_ORIG fn_007df8b0

/* The battle camera's start (native/camera.c): CArenaCameraConfigurator
 * (vtable 0xfb48e4) slot 8, found through its RTTI. */
#define HOMM5_BATTLE_DEFAULTS_ORIG fn_007d5ee0
/* The battle screen's camera operator (native/camera.c zoom_view_camera): the
 * battle's camera setup 0074c2b0 repeats the base game's 0074bcd0 call for
 * call and keeps the operator, made by CCameraOperator's constructor 007dde30
 * (found by its vtable 0xfb4e88), here; the base game's +0x4e4. */
#define HOMM5_COMBAT_OPERATOR 0x3a4

/* Hor+ (native/horplus.c): the projection 009ffda0 has the base game's
 * 00a1a1e0 arguments (h/w, fov, znear, zfar, offset) but always keeps the
 * horizontal angle; there is no wide-screen angle function to pass through. */
#define HOMM5_FN_PROJECTION fn_009ffda0

/* Interface bounds and stretched roots (native/ui_bounds.c): the base game's
 * axis 00e43650 is 00e4eba0 here, asked for x from SetParentRect 00e476c0 at
 * 00e477ce; the screen spans hof_screen_span (native/hof_screen.c). */
#define HOMM5_FN_ANCHOR_AXIS fn_00e4eba0
#define HOMM5_RET_ANCHOR_X 0x00e477d3
#define HOMM5_RET_ANCHOR_Y 0x00e4781d /* and for y (the dragged icon, native/ui_bounds.c; the phone rows, native/phone_layout.c) */
/* native/phone_layout_table.h: no wait button in a battle. */
#define HOMM5_PHONE_LAYOUT_HOF 1
#define HOMM5_SCREEN_SPAN hof_screen_span
#define HOMM5_STRETCH_ROOTS 1
/* A battle's stack counter (native/hof_screen.c hof_counter_window). */
#define HOMM5_COUNTER_WINDOW hof_counter_window
/* Screen mode changes (native/hof_screen.c): the roots are laid out again
 * after one (native/phone_hud.c). */
#define HOMM5_SCREEN_MODE_CHANGES hof_screen_mode_changes

/* The phone's interface (native/phone_hud.c), from the base game's through
 * byte-identical functions, RTTI and the classes' sub-object offsets
 * (research/phone-hud-expansions-addresses.md, 2026-10-19). "probable": the
 * town's, hire, build, hero and loading screens' IScreen vtables and offsets
 * (by structure), the clipped draw 00e51010 and the loading screen's
 * constructor 0082ef90; the rest hash-identical. This game has no root
 * placement (the base game's 00e74f30): a root takes its 1024x768 rect as it
 * is described (00e7f630), the safe area goes into its edge windows' anchors
 * (native/ui_bounds.c), and its conversions have no virtual size in memory. */
#define HOMM5_VT_TOWN 0x00fc407c /* NUI::CTownScreen's IScreen, at +0x484 */
#define HOMM5_SUB_TOWN 0x484
#define HOMM5_VT_LOADING 0x00fb87bc /* NUI::CPWLScreenInterface's, at +0x1c0 */
#define HOMM5_SUB_LOADING 0x1c0
#define HOMM5_VT_HIRE 0x00fc37ac /* NUI::CHireScreen's, at +0x27c */
#define HOMM5_SUB_HIRE 0x27c
#define HOMM5_VT_BUILD 0x00fc96f8 /* NUI::CTownBuildScreen's, at +0x258 */
#define HOMM5_SUB_BUILD 0x258
#define HOMM5_VT_HERO 0x00fb926c /* NUI::CHeroScreen2's, at +0x794 */
#define HOMM5_SUB_HERO 0x794
#define HOMM5_VT_LOADING_MAIN 0x00fb8758 /* NUI::CPWLScreenInterface */
#define HOMM5_VT_MS_BUTTON 0x01032cf8
/* The drop-down list of a combo box (the options' screen resolution: it is
 * placed under its field from the interface's left edge, native/ui_bounds.c). */
#define HOMM5_VT_COMBO_LIST 0x01033278
#define HOMM5_VT_EDIT_LINE 0x01036dec
#define HOMM5_VT_EDIT_BOX 0x0103c300
#define HOMM5_VT_LIST 0x01033278
#define HOMM5_VT_SCROLLER 0x01037c40
#define HOMM5_VT_TEXT_VIEW 0x01038670
#define HOMM5_HUD_ROOT_RECT 1
#define HOMM5_UI_REAL_W 0x010d4e2c /* the effective width native/hof_screen.c keeps there */
#define HOMM5_UI_REAL_H 0x010d4e30
#define HOMM5_UI_ORIGIN_X 0x012875bc
#define HOMM5_UI_ORIGIN_Y 0x012875c0
#define HOMM5_FN_WINDOW_DRAW fn_00e49cb0
#define HOMM5_FN_WINDOW_CONTAINS fn_00e47cd0
#define HOMM5_FN_SIMPLE_DRAW fn_00e51010
#define HOMM5_FN_TEXT_LAYOUT fn_00e4f990
#define HOMM5_ADDR_TEXT_LAYOUT 0x00e4f990u
#define HOMM5_FN_LOADING_SCREEN fn_0082ef90
#define HOMM5_FN_ROOT_DESCRIBE fn_00e7f630
#define HOMM5_SCREEN_DESC 0xc4 /* where 00e7f630 keeps the description (the base game's +0xc8) */
/* SetParentRect (thiscall: ECX the window, the parent's rect on the stack):
 * a screen laid out again when the camera housing changes sides. */
#define HOMM5_ADDR_SET_PARENT_RECT 0x00e476c0u

/* The touch camera's ground pick (native/camera.c): the 3D picture has the
 * screen's own shape, whatever the virtual size says. */
#define HOMM5_CAMERA_PICTURE_SHAPE 1
/* CSkeletonAnimator's NeedUpdate, the translated original (base 0067c060). */
#define HOMM5_ANIM_NEED_UPDATE_ORIG fn_006a5a70

/* native/overrides.c: the x87 float max/min helpers (the base game's
 * 00614540 / 00614570); no counterpart of the box grow 0066e070. */
#define HOMM5_FLOAT_MAX_ORIG fn_0063d930
#define HOMM5_FLOAT_MIN_ORIG fn_0063d960

/* native/scene.c: the index buffer's fill and its "no base" flag, a vertex
 * pool's range, the cull tests and their sign mask (the base game's
 * 0060d260 / 0x011375e8, 0060d810, 00a1a680 / 00a1a9f0 / 00a1ab50,
 * 0x00f94980). */
#define HOMM5_IB_FILL_ORIG fn_00636690
#define HOMM5_IB_NOBASE 0x01136ce0u
#define HOMM5_POOL_ALLOC_ORIG fn_00636c40
#define HOMM5_CULL_TEST_ORIG fn_00a00280
#define HOMM5_CULL_PUSH_ORIG fn_00a005f0
#define HOMM5_CULL_OBJECT_ORIG fn_00a00750
#define HOMM5_CULL_SIGNMASK 0x00f9d6c0u

/* native/granny.c: granny2.dll is the base game's, the defaults there. */
/* native/lighting.c: 00698d00 (the base game's 0066f2e0) and its reciprocal
 * square root table. */
#define HOMM5_LIGHT_VERTEX_ORIG fn_00698d00
#define HOMM5_LIGHT_RSQRT 0x01138900u
/* native/gpu_skin.cpp (the base game's in brackets): the part fill
 * (0066e4d0), the staged copy (0066d950), the format 5 allocation
 * (00606af0), the alpha 006b9670 (0068fc20) and its vectors (01138ee8 /
 * 01138ef4), the tables (01139088 / 01142088), gfx_low_ram (010cd1f9) and
 * the bounds' positions (0066f600). */
#define H5SKIN_FILL_ORIG fn_00697ef0
#define H5SKIN_COPY_ORIG fn_00697370
#define H5SKIN_ALLOC_ORIG fn_0062ffa0
#define H5SKIN_ALPHA_FN 0x006b9670u
#define H5SKIN_ALPHA_A 0x01138760u
#define H5SKIN_ALPHA_B 0x0113876cu
#define H5SKIN_RSQRT 0x01138900u
#define H5SKIN_CBRT 0x01141900u
#define H5SKIN_LOW_RAM 0x010cf1e1u
#define H5SKIN_POSITIONS_ORIG fn_00699020

/* The end of a frame's 3D world (native/render_split.c): CGameView::Draw
 * (the NGScene::CGameView vtable's slot 0x50) and where the world pass gets
 * back from it - its main, second and alternate view. */
#define HOMM5_FN_GAME_VIEW_DRAW fn_006565e0
#define HOMM5_RET_VIEW_MAIN 0x007bcbe1u
#define HOMM5_RET_VIEW_SECOND 0x007bcc0du
#define HOMM5_RET_VIEW_ALT 0x007bc912u
