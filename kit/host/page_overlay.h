// page_overlay.h - draw the settings page on a frame the HOST owns.
//
// WHY THIS EXISTS RATHER THAN THREE COPIES OF IT. Every presenter is handed a
// pointer to the guest's own DirectDraw surface. Drawing the page there would
// corrupt pixels the game is still reading and would change what the guest
// itself sees - a mod's overlay would become part of the simulation's input.
// So the page is drawn on a copy, and the presenter converts the copy.
//
// Three hosts present frames (headless, smoke, windowed) and all three have
// the same obligation, so the rule lives in one place where it can be tested
// once instead of being restated three times and drifting.
#pragma once
#include <stdint.h>

// Whether the mod runtime is live in this process. False until a host says
// otherwise, so a build with mods disabled - RECOMP_NO_MODS, or a host that
// sets BootOptions::load_mods false - never registers the page's keyboard and
// never draws it. That matters beyond the drawing: the page's key handler
// consumes F10 unconditionally and every key while it is open, so registering
// it with mods off would change what the guest sees.
void host_page_set_enabled(bool enabled);
bool host_page_enabled(void);

// Returns host-owned storage holding `pixels` with the settings page drawn on
// it. The returned pointer is valid until the next call on this thread. The
// bytes behind `pixels` are never written.
//
// `pitch` is the surface's stride in bytes; a non-positive one is treated as
// the tightly packed width, which is what the shims pass for a locked surface
// with no padding.
const void *host_page_overlay(const void *pixels, int w, int h, int bpp, int pitch,
                              const uint32_t *palette);

#include <vector>
// Transparent outside the page. Generated under the guest baton, copied into
// the sealed frame; the presenter never calls the mod/settings registries.
bool host_page_rgba(std::vector<uint8_t> *rgba);
// The tabbed settings menu at the size of the game's rectangle on screen (mods_menu_render):
// 0 not showing, 1 unchanged since *generation, 2 redrawn into *pixels (w*h*4, straight alpha).
// An open prompt (below) is drawn instead.
int host_page_menu(int w, int h, uint64_t *generation, const uint8_t **pixels);

// ---------------------------------------------------------------------------
// A host prompt over the game: the ubi-gs server question (server_prompt.h),
// which a guest thread waits on, inside an import, until it is answered.
// While it is open it is modal, and it lives where the tabbed settings menu
// does: drawn in the menu's place at the game rectangle's size - on the frame
// being sealed when the game keeps presenting, on the presenter's repeat of
// the last one when the waiting thread is the one that presents (Heroes V's
// main thread) - and handed the keyboard, the pointer (as fractions of that
// page) and typed text before the menu or the game sees them. The waiting
// thread sleeps with the baton handed on, so the run thread's idle slices
// keep the window's events coming. Installed by the host that can show it,
// before the guest starts; without one nothing here does anything.
// ---------------------------------------------------------------------------
struct HostPagePrompt {
    bool (*open)(void);
    // As host_page_menu: 1 unchanged since *generation, 2 redrawn.
    int (*render)(int w, int h, uint64_t *generation, const uint8_t **pixels);
    // A DirectInput key; true when the prompt took it (a press while it is
    // open, and that press's release whenever it comes).
    bool (*key)(uint8_t dik, bool down);
    void (*text)(const char *utf8); // typed or pasted text
    // u, v over the page; button -1 is motion; wheel in WM_MOUSEWHEEL units.
    void (*pointer)(float u, float v, int button, int down, int wheel);
    bool (*wants_text)(void); // text input (a phone's keyboard) should be up
    void (*cancel)(void);     // the app is closing: the waiting guest gets "no"
};
void host_page_set_prompt(const HostPagePrompt *prompt);
bool host_page_prompt_open(void);
// The prompt alone, as host_page_menu draws it: for the presenter, which puts
// it on the frames it repeats while the guest that asked presents none.
int host_page_prompt_render(int w, int h, uint64_t *generation, const uint8_t **pixels);
bool host_page_prompt_key(uint8_t dik, bool down);
// False when no prompt is open: the text is not the prompt's.
bool host_page_prompt_text(const char *utf8);
// True (consumed) whenever the prompt is open.
bool host_page_prompt_pointer(float u, float v, int button, bool down, int wheel);
bool host_page_prompt_wants_text(void);
void host_page_prompt_cancel(void);
// The settings page or a prompt is up: the pointer is the player's, not the
// game's (no capture, the system cursor shown, the pad held back).
bool host_page_visible(void);
