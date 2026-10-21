// native/save.cpp - the guest call native/touch.c needs (guest_call is C++).
#include "guest.h"

// The game's own address (see touch.c): an expansion's addresses.h names it.
#ifdef RECOMP_ADDRESS_HEADER
#include RECOMP_ADDRESS_HEADER
#endif
#ifndef HOMM5_SAVE_SETTINGS
#define HOMM5_SAVE_SETTINGS 0x006bfe80
#endif

enum : uint32_t {
    SAVE_SETTINGS = HOMM5_SAVE_SETTINGS, // writes global.cfg and the profile's user.cfg (as at exit)
};

extern "C" void homm5_save_settings(X86 *c) {
    guest_call(c, SAVE_SETTINGS);
}

// A thiscall with no stack arguments (native/widescreen.c re-places a screen).
// The caller's registers but EAX and EDX come back as they were.
extern "C" void homm5_guest_thiscall(X86 *c, uint32_t fn, uint32_t self) {
    const uint32_t ecx = c->r[R_ECX];
    c->r[R_ECX] = self;
    guest_call(c, fn);
    c->r[R_ECX] = ecx;
}

// A thiscall with one stack argument, a rectangle (x1, y1, x2, y2 floats)
// passed by its address: it goes on the guest's stack below the caller's
// (native/phone_hud.c lays a screen out again with SetParentRect).
extern "C" void homm5_guest_thiscall_rect(X86 *c, uint32_t fn, uint32_t self, const float r[4]) {
    const uint32_t ecx = c->r[R_ECX], esp = c->r[R_ESP];
    const uint32_t rect = (esp - 32u) & ~15u;
    for (int i = 0; i < 4; ++i)
        wrf32(rect + 4u * (uint32_t)i, r[i]);
    c->r[R_ESP] = rect;
    c->r[R_ECX] = self;
    guest_call(c, fn, rect);
    c->r[R_ECX] = ecx;
    c->r[R_ESP] = esp;
}

// Any call: ECX and EDX as the callee wants them, the stack arguments pushed
// below `stack_top` (an address under the caller's ESP, so the caller can keep
// its own guest scratch between the two), or below ESP when it is zero. ECX,
// EDX and ESP come back as they were; returns EAX (native/tap_attack.c).
extern "C" uint32_t homm5_guest_call_regs(X86 *c, uint32_t fn, uint32_t ecx, uint32_t edx,
                                          const uint32_t *args, int nargs, uint32_t stack_top) {
    const uint32_t saved_ecx = c->r[R_ECX], saved_edx = c->r[R_EDX], saved_esp = c->r[R_ESP];
    if (stack_top)
        c->r[R_ESP] = stack_top;
    c->r[R_ECX] = ecx;
    c->r[R_EDX] = edx;
    const uint32_t eax = guest_call(c, fn, args, nargs);
    c->r[R_ECX] = saved_ecx;
    c->r[R_EDX] = saved_edx;
    c->r[R_ESP] = saved_esp;
    return eax;
}
