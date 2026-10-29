// layout.cpp - see layout.h.
#include "layout.h"

#include <algorithm>

namespace launcher::native {

Look choose_look(float width, float height, bool have_skin) {
    Look l;
    l.wide = width >= 760 && height >= 480 && (width > height || width >= 1000);
    // A desktop window sideways, as the game itself plays, gets the game's
    // look and scales to fit; upright or very small, the flat one.
    const bool landscape = width > height && width >= 640 && height >= 320;
    l.skinned = have_skin && (l.wide || landscape);
    l.scale = l.skinned ? std::min({1.0f, width / 1200, height / 760}) : 1;
    l.width = width / l.scale;
    l.height = height / l.scale;
    return l;
}

float modal_scale(float width, float height, float natural_width, float natural_height) {
    return std::max(0.05f,
                    std::min({1.0f, (width - 32) / natural_width, (height - 32) / natural_height}));
}

std::vector<FlowPlace> flow(const std::vector<float> &widths, float width, float spacing) {
    std::vector<FlowPlace> out;
    int row = 0;
    float x = 0;
    bool empty = true;
    for (float w : widths) {
        if (!empty && x + spacing + w > width) {
            ++row;
            x = 0;
            empty = true;
        }
        if (!empty)
            x += spacing;
        out.push_back({row, x});
        x += w;
        empty = false;
    }
    return out;
}

} // namespace launcher::native
