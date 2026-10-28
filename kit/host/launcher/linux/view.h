// view.h - the native launcher's screens on Linux, drawn on the canvas: the
// game's own look while its textures are on this computer
// (apple/LauncherSkinView.swift), the flat look otherwise
// (apple/LauncherView.swift), the progress, settings and alert dialogs over
// them. Laid out and drawn afresh for each frame; what can be clicked is
// what the last frame drew.
#pragma once
#include "../launcher_canvas.h"
#include "model.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace launcher::native {

class View {
  public:
    explicit View(Model &model) : model_(model) {}
    // Lays out and draws the launcher over all of `canvas`, `density` pixels
    // a point.
    void draw(Canvas &canvas, float density);
    // The pointer at (x, y) in the canvas's pixels: moved, pressed, let go,
    // gone from the window. True when the screen changes.
    bool pointer(float x, float y);
    bool press(float x, float y);
    bool release(float x, float y);
    bool leave();
    // The wheel, in lines down.
    bool wheel(float lines);
    // A key (SDL_Keycode) with its modifiers (SDL_Keymod).
    bool key(uint32_t key, uint16_t mods);
    // Something is dragged over the window.
    bool dragging(bool on);
    bool settings_shown() const {
        return settings_;
    }

    // The frame being drawn (view.cpp), and what it keeps between frames.
    struct Ui;
    bool settings_ = false, menu_ = false;
    Rect menu_anchor_;
    // The flat column's scroll, in points.
    float scroll_ = 0, scroll_max_ = 0;
    // The buttons Return and Escape press on the screen drawn last.
    std::function<void()> default_action_, cancel_action_;
    // Pictures made from others for one size: shadows, backdrops, cards.
    std::map<std::string, Image> cache_;
    std::map<const Image *, Image> white_;

  private:
    friend struct Ui;
    struct Region {
        Rect r;
        std::string key;
        std::function<void()> action;
        bool enabled = true;
    };
    // The topmost region under (x, y), or nullptr.
    const Region *at(float x, float y) const;
    bool update_hover();

    Model &model_;
    std::vector<Region> regions_;
    std::string hover_, pressed_;
    float mx_ = -1, my_ = -1;
    bool drag_ = false;
};

} // namespace launcher::native
