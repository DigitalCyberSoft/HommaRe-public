// trackpad_mac.h - the Mac trackpad's scroll, pinch and turn, read from AppKit
// with their phases and inertia, which SDL's wheel event leaves out.
#pragma once
#include <SDL3/SDL.h>

#include "../input_trackpad.h"

// Watches the window's scroll, magnify and rotate events. The handler runs on
// the main thread, inside the SDL event pump; returning true drops the event
// (SDL never sees it), false hands it on to SDL as before. Once per window.
bool trackpad_mac_install(SDL_Window *window, bool (*handler)(const TrackpadEvent &));

// Unattended runs (RECOMP_AUTO_KEYS "trackpad:", sdl/main.cpp): one event as the
// trackpad would send it - kind 0 a vertical scroll, 1 a horizontal one, 2 a
// pinch, 3 a turn; phase and momentum in CGEvent's numbering (scroll phase 1
// began, 2 changed, 4 ended, 128 may begin; momentum 1 begin, 2 continue,
// 3 end; a pinch's or turn's phase 1 began, 2 changed, 4 ended) - made into
// the NSEvent AppKit would hand over and given to the installed handler.
void trackpad_mac_test(int kind, int phase, int momentum, double value);
