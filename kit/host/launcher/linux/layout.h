// layout.h - the native launcher's layout decisions on Linux, apart from the
// drawing so the tests can check them: which look a window gets and at what
// scale (LauncherRootView in apple/LauncherView.swift), how a modal shrinks to
// fit (SkinModal), and views laid side by side that wrap (SkinFlow).
#pragma once
#include <vector>

namespace launcher::native {

struct Look {
    // The game's own look (its textures are on this computer and there is
    // room), else the flat one; the flat look's list beside a large card
    // where the window is wide, a column otherwise.
    bool skinned = false, wide = false;
    // The skinned screens are laid out at no less than 1200 x 760 points and
    // scaled down to fit, as the game draws its own: `scale` (at most 1) and
    // the size they are laid out at.
    float scale = 1, width = 0, height = 0;
};
Look choose_look(float width, float height, bool have_skin);

// A dialog of `natural` size over a window: scaled down to leave 16 points
// round it, never up.
float modal_scale(float width, float height, float natural_width, float natural_height);

// Items of `widths` side by side, `spacing` apart, onto the next row where
// `width` runs out: each item's row (0 first) and x.
struct FlowPlace {
    int row;
    float x;
};
std::vector<FlowPlace> flow(const std::vector<float> &widths, float width, float spacing);

} // namespace launcher::native
