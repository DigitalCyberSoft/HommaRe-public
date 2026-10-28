/* native/screens_duel.c - Hot seat's duels drawn natively
 * (native/native_screens.c): NUI::CHSDMWaitScreen<CHeroInfoView> ("duel", one
 * hero each) and <CTripleHeroesView> ("duel3", three each).
 *
 * The two players choose one after the other from the game's ready heroes
 * (presets): the first picks and goes on, the second picks and starts. The
 * document gives whose turn it is, what each player holds (a preset index,
 * or random), the count of presets and the settings (arena, the battle's
 * speed); the view reads the presets' names and portraits from the game's
 * own table in the paks (UI/MPDMLobby/presets.(DuelPresets).xdb, in grid
 * order while custom presets are off). A pick goes through the screen's grid
 * (one hero) or its slot setter (three), as a click or a drop would.
 * The base game's addresses: research notes phone-menus/native/re-duels.md. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native_screens.h"
#include "x86.h"

#ifdef RECOMP_OVERRIDE_HEADER
#include RECOMP_OVERRIDE_HEADER
#endif
#ifdef RECOMP_ADDRESS_HEADER
#include RECOMP_ADDRESS_HEADER
#endif

/* The base game's; an expansion has none yet (native/native_screens.c). */
#ifndef RECOMP_ADDRESS_HEADER
#define NS_VT_DUEL 0x00fc88fc       /* CHSDMWaitScreen<CHeroInfoView>'s IScreen, at +0x1d0 */
#define NS_VT_DUEL3 0x00fc8854      /* CHSDMWaitScreen<CTripleHeroesView>'s */
#define NS_SUB_DUEL 0x1d0
#define NS_DUEL_NEXT 0x009add80     /* "start": the second player's turn, or the game */
#define NS_DUEL_BACK 0x009adce0     /* "back": the first player's turn, or close */
#define NS_DUEL3_NEXT 0x009addd0
#define NS_DUEL3_BACK 0x009add30
#define NS_DUEL3_PUT 0x009ed880     /* thiscall (view, slot, preset, random), RET 0xc */
#define NS_DUEL3_VIEW 0x009ed9f0    /* fastcall ECX view: its slots redrawn */
#define NS_DUEL3_MOVER 0x009edd20   /* fastcall ECX mover: the greyed presets */
#define NS_DUEL_ARENAS 0x009e2a40   /* fastcall ECX 0x011756dc: SDuelArenas* */
#define NS_DUEL_ARENAS_OF 0x011756dc
#define NS_DUEL_SPEED_NAME 0x00b97ca0 /* fastcall ECX speed 0..3: const wstr* */
#define NS_TEXT_OF_REF 0x00a09de0   /* fastcall ECX a text ref: const wstr* */
#endif

#if defined(NS_VT_DUEL)

#ifndef NS_DUEL_SETTINGS_SIZE
#define NS_DUEL_SETTINGS_SIZE 0x14 /* the panel's settings block */
#endif
/* Fields and slots an expansion moves: its addresses.h defines the ones that
 * differ (research notes phone-menus/native/expansion-*-addresses.md). */
#ifndef NS_P_GRID
#define NS_P_GRID 0x44
#endif
#ifndef NS_P_SET_SETTINGS
#define NS_P_SET_SETTINGS 0x34
#endif
#ifndef NS_P_GET_SETTINGS
#define NS_P_GET_SETTINGS 0x38
#endif
#ifndef NS_S_SPEED
#define NS_S_SPEED 0x10
#endif
#ifndef NS_DUEL_INDEX
#define NS_DUEL_INDEX 0xec
#endif
#ifndef NS_TRIPLE_SLOT_SIZE
#define NS_TRIPLE_SLOT_SIZE 0xe4
#endif
#ifndef NS_TRIPLE_INDEX
#define NS_TRIPLE_INDEX 0xe0
#endif

enum {
    D_PANEL = 0x17c,         /* CDMScreenView* */
    D_VIEWS = 0x180,         /* vector of the 2 players' views */
    D_TURN = 0x18c,          /* the current view */
    D_MOVER = 0x194,         /* 3x3: the drag and drop mover */
    D_COUNT = 0x198,         /* presets */
    P_START = 0x14,          /* the panel: Start (Next) IButton* */
    P_GRID = NS_P_GRID,           /* CDMPresetsTable*: vt[0] SetSelected(i) */
    P_SET_SETTINGS = NS_P_SET_SETTINGS,   /* vtable slots, thiscall (block*) */
    P_GET_SETTINGS = NS_P_GET_SETTINGS,
    /* the settings block (NS_DUEL_SETTINGS_SIZE bytes) */
    S_ARENA = 0x00,
    S_SPEED = NS_S_SPEED,
    /* a choice (a duel view at +0x0c, a 3x3 slot block) */
    C_SET = 0x00,
    C_RANDOM = 0x01,
    DUEL_CHOICE = 0x0c,
    DUEL_INDEX = NS_DUEL_INDEX,       /* the duel view: its preset index */
    DUEL_SHOWN = 0x08,
    TRIPLE_SLOTS = 0x10,     /* the 3x3 view: vector of 3 blocks of 0xe4 */
    TRIPLE_SLOT_SIZE = NS_TRIPLE_SLOT_SIZE,
    TRIPLE_INDEX = NS_TRIPLE_INDEX,
    TRIPLE_SHOWN = 0x0c,
    ARENA_SIZE = 0x24,
};

static int g_triple; /* the variant being read */

static uint32_t view_of(uint32_t screen, int k) {
    const uint32_t b = rd32(screen + D_VIEWS), e = rd32(screen + D_VIEWS + 4);
    return ns_ptr(b) && e > b && (uint32_t)k < (e - b) / 4u ? rd32(b + 4u * (uint32_t)k) : 0;
}

static int turn_of(uint32_t screen) {
    return rd32(screen + D_TURN) == view_of(screen, 1) ? 1 : 0;
}

static void settings(Scratch *s, uint32_t screen, uint32_t block) {
    const uint32_t panel = rd32(screen + D_PANEL);
    if (ns_ptr(panel))
        ns_vcall(s, panel, P_GET_SETTINGS, &block, 1);
}

static void handler(Scratch *s, uint32_t screen, uint32_t fn) {
    const uint32_t message = 0;
    ns_call(s, fn, screen, 0, &message, 1);
}

static void duel_command(Scratch *s, uint32_t screen, const char *cmd) {
    char verb[16];
    ns_cmd_str(cmd, "do", verb, sizeof verb);
    const int count = (int)rd32(screen + D_COUNT);
    if (strcmp(verb, "pick") == 0) {
        /* The current player's hero: a preset, or -1 for random. */
        const int i = ns_cmd_int(cmd, "index", -1);
        if (i >= count)
            return;
        if (!g_triple) {
            const uint32_t panel = rd32(screen + D_PANEL);
            const uint32_t grid = ns_ptr(panel) ? rd32(panel + P_GRID) : 0;
            /* The random cell is the last one (index = count). */
            const uint32_t at = i < 0 ? (uint32_t)count : (uint32_t)i;
            if (ns_ptr(grid))
                ns_vcall(s, grid, 0x00, &at, 1);
        } else {
            const int slot = ns_cmd_int(cmd, "slot", -1);
            const uint32_t view = rd32(screen + D_TURN);
            if (slot < 0 || slot > 2 || !ns_ptr(view))
                return;
            /* The game's own rule: one preset once per player. */
            const uint32_t b = rd32(view + TRIPLE_SLOTS);
            for (int k = 0; k < 3 && i >= 0 && ns_ptr(b); ++k) {
                const uint32_t blk = b + TRIPLE_SLOT_SIZE * (uint32_t)k;
                if (k != slot && rd8(blk + C_SET) && !rd8(blk + C_RANDOM) &&
                    (int)rd32(blk + TRIPLE_INDEX) == i)
                    return;
            }
            const uint32_t args[3] = {(uint32_t)slot, i < 0 ? 0u : (uint32_t)i, i < 0 ? 1u : 0u};
            ns_call(s, NS_DUEL3_PUT, view, 0, args, 3);
            ns_call(s, NS_DUEL3_VIEW, view, 0, NULL, 0);
            if (ns_ptr(rd32(screen + D_MOVER)))
                ns_call(s, NS_DUEL3_MOVER, rd32(screen + D_MOVER), 0, NULL, 0);
        }
    } else if (strcmp(verb, "next") == 0) {
        handler(s, screen, g_triple ? NS_DUEL3_NEXT : NS_DUEL_NEXT);
    } else if (strcmp(verb, "back") == 0) {
        handler(s, screen, g_triple ? NS_DUEL3_BACK : NS_DUEL_BACK);
    } else if (strcmp(verb, "arena") == 0 || strcmp(verb, "speed") == 0) {
        /* Settings are the first player's to change. */
        const uint32_t panel = rd32(screen + D_PANEL);
        if (turn_of(screen) != 0 || !ns_ptr(panel))
            return;
        const uint32_t block = ns_alloc(s, NS_DUEL_SETTINGS_SIZE);
        settings(s, screen, block);
        const int v = ns_cmd_int(cmd, "index", -1);
        if (strcmp(verb, "speed") == 0) {
            if (v < 0 || v > 3)
                return;
            wr32(block + S_SPEED, (uint32_t)v);
        } else {
            const uint32_t arenas = ns_call(s, NS_DUEL_ARENAS, NS_DUEL_ARENAS_OF, 0, NULL, 0);
            const uint32_t ab = ns_ptr(arenas) ? rd32(arenas + 0x44) : 0, ae = ns_ptr(arenas) ? rd32(arenas + 0x48) : 0;
            if (v < 0 || !ns_ptr(ab) || (uint32_t)v >= (ae - ab) / ARENA_SIZE)
                return;
            wr32(block + S_ARENA, (uint32_t)v);
        }
        ns_vcall(s, panel, P_SET_SETTINGS, &block, 1);
    }
}

/* A choice's preset index, -1 for random, -2 for none yet. */
static int choice(uint32_t block, uint32_t index_at) {
    if (!rd8(block + C_SET))
        return -2;
    return rd8(block + C_RANDOM) ? -1 : (int)rd32(index_at);
}

static void duel_document(Json *j, Scratch *s, uint32_t screen) {
    char diff[64], text[256];
    ns_difficulty_name(s, 0, diff, sizeof diff);
    ns_ru = (unsigned char)diff[0] >= 0xd0;
    const int turn = turn_of(screen);
    j_lit(j, "{");
    j_kv_str(j, "screen", g_triple ? "duel3" : "duel");
    j_lit(j, ",");
    j_kv_str(j, "lang", ns_ru ? "ru" : "en");
    j_lit(j, ",");
    j_kv_str(j, "title", g_triple ? ns_tr("Дуэль 3х3", "Duel 3x3") : ns_tr("Дуэль", "Duel"));
    j_printf(j, ",\"turn\":%d,\"count\":%d,\"players\":[", turn, (int)rd32(screen + D_COUNT));
    for (int k = 0; k < 2; ++k) {
        const uint32_t view = view_of(screen, k);
        if (k)
            j_lit(j, ",");
        j_lit(j, "{\"picks\":[");
        if (ns_ptr(view) && !g_triple) {
            j_printf(j, "%d", choice(view + DUEL_CHOICE, view + DUEL_INDEX));
        } else if (ns_ptr(view)) {
            const uint32_t b = rd32(view + TRIPLE_SLOTS);
            for (int slot = 0; slot < 3 && ns_ptr(b); ++slot) {
                const uint32_t blk = b + TRIPLE_SLOT_SIZE * (uint32_t)slot;
                j_printf(j, "%s%d", slot ? "," : "", choice(blk, blk + TRIPLE_INDEX));
            }
        }
        const int shown = ns_ptr(view) && rd8(view + (g_triple ? TRIPLE_SHOWN : DUEL_SHOWN));
        j_printf(j, "],\"shown\":%s}", shown ? "true" : "false");
    }
    j_lit(j, "]");

    /* The settings, the first player's to change. */
    const uint32_t block = ns_alloc(s, NS_DUEL_SETTINGS_SIZE);
    for (uint32_t i = 0; i < NS_DUEL_SETTINGS_SIZE; i += 4)
        wr32(block + i, 0);
    settings(s, screen, block);
    const uint32_t arenas = ns_call(s, NS_DUEL_ARENAS, NS_DUEL_ARENAS_OF, 0, NULL, 0);
    const uint32_t ab = ns_ptr(arenas) ? rd32(arenas + 0x44) : 0, ae = ns_ptr(arenas) ? rd32(arenas + 0x48) : 0;
    const int narenas = ns_ptr(ab) && ae > ab && ae - ab <= ARENA_SIZE * 64u ? (int)((ae - ab) / ARENA_SIZE) : 0;
    j_lit(j, ",\"arenas\":[");
    for (int i = 0; i < narenas; ++i) {
        ns_text_of(s, NS_TEXT_OF_REF, ab + ARENA_SIZE * (uint32_t)i + 0xc, text, sizeof text);
        if (i)
            j_lit(j, ",");
        j_str(j, text);
    }
    j_printf(j, "],\"arena\":%d,\"speeds\":[", (int)rd32(block + S_ARENA));
    for (int i = 0; i < 4; ++i) {
        ns_text_of(s, NS_DUEL_SPEED_NAME, (uint32_t)i, text, sizeof text);
        if (i)
            j_lit(j, ",");
        j_str(j, text);
    }
    j_printf(j, "],\"speed\":%d,\"settings_enabled\":%s", (int)rd32(block + S_SPEED),
             turn == 0 ? "true" : "false");
    j_lit(j, ",");
    j_kv_str(j, "arena_label", ns_tr("Арена", "Arena"));
    j_lit(j, ",");
    j_kv_str(j, "speed_label", ns_tr("Скорость боя", "Battle speed"));
    j_lit(j, ",");
    j_kv_str(j, "random", ns_tr("Случайный герой", "Random hero"));
    j_lit(j, ",\"player_names\":[");
    j_str(j, ns_tr("Первый игрок", "First player"));
    j_lit(j, ",");
    j_str(j, ns_tr("Второй игрок", "Second player"));

    const uint32_t panel = rd32(screen + D_PANEL);
    const int start_ok = ns_ptr(panel) && ns_enabled(s, ns_iwindow(rd32(panel + P_START)));
    j_lit(j, "],\"buttons\":[");
    j_button(j, 1, "next", turn == 0 ? ns_tr("Следующий", "Next") : ns_tr("Начать", "Start"), start_ok);
    j_button(j, 0, "back", turn == 0 ? ns_tr("Назад", "Back") : ns_tr("Предыдущий", "Previous"), 1);
    j_lit(j, "]}");
}

#define VARIANT(name, triple, fn, ...) \
    static void name(__VA_ARGS__) { g_triple = triple; fn; }
VARIANT(duel1_command, 0, duel_command(s, screen, cmd), Scratch *s, uint32_t screen, const char *cmd)
VARIANT(duel1_document, 0, duel_document(j, s, screen), Json *j, Scratch *s, uint32_t screen)
VARIANT(duel3_command, 1, duel_command(s, screen, cmd), Scratch *s, uint32_t screen, const char *cmd)
VARIANT(duel3_document, 1, duel_document(j, s, screen), Json *j, Scratch *s, uint32_t screen)

const NsScreen ns_duel = {.id = "duel", .vtable = NS_VT_DUEL, .sub = NS_SUB_DUEL,
                          .command = duel1_command, .document = duel1_document};
const NsScreen ns_duel3 = {.id = "duel3", .vtable = NS_VT_DUEL3, .sub = NS_SUB_DUEL,
                           .command = duel3_command, .document = duel3_document};

#else

/* No addresses for this game: never matched (a zero vtable). */
const NsScreen ns_duel = {.id = "duel"};
const NsScreen ns_duel3 = {.id = "duel3"};

#endif
