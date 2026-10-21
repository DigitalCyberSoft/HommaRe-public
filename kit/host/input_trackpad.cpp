// input_trackpad.cpp - see input_trackpad.h.
#include "input_trackpad.h"

#include <math.h>

#include <algorithm>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// battle: a battlefield takes this gesture too (a scroll or a pinch: its camera
// zooms and pans, a turn it does not).
TrackpadMapper::Owner TrackpadMapper::claim(const TrackpadView &v, bool wanted, bool battle) const {
    // A pinch or a turn that joins a gesture under way is part of it, whatever
    // the pointer shows by now.
    if (open_)
        return Camera;
    if (!wanted || v.width <= 0 || v.height <= 0)
        return Game;
    return v.world || (battle && v.zoom_view) ? Camera : Game;
}

// The wheel's own direction (the system's flip undone): up is nearer, as a
// mouse wheel turned away from the player.
double TrackpadMapper::height_up(const TrackpadEvent &e) const {
    const double up = e.inverted ? -e.dy : e.dy;
    return invert_height_ ? -up : up;
}

void TrackpadMapper::begin(const TrackpadView &v, std::vector<TrackpadAction> *out) {
    // Height mode on the map keeps the middle of the picture where it is, so
    // only the distance changes; Tablet mode and a battle's zoom hold the
    // ground under the pointer.
    ax_ = anchor_cursor_ ? v.cursor_x : v.width / 2;
    ay_ = anchor_cursor_ ? v.cursor_y : v.height / 2;
    px_ = py_ = 0;
    zoom_ = 1;
    turn_ = 0;
    open_ = true;
    out->push_back({0, ax_, ay_, zoom_, turn_});
}

void TrackpadMapper::move(std::vector<TrackpadAction> *out) {
    out->push_back({1, ax_ + px_, ay_ + py_, zoom_, turn_});
}

void TrackpadMapper::end_if_idle(std::vector<TrackpadAction> *out) {
    if (!open_ || scroll_.live || magnify_.live || rotate_.live)
        return;
    out->push_back({2, ax_ + px_, ay_ + py_, zoom_, turn_});
    open_ = false;
}

void TrackpadMapper::cancel(std::vector<TrackpadAction> *out) {
    scroll_ = magnify_ = rotate_ = Stream{};
    end_if_idle(out);
}

void TrackpadMapper::scroll(const TrackpadEvent &e, const TrackpadView &v,
                            std::vector<TrackpadAction> *out) {
    if (!open_)
        begin(v, out);
    if (mode_ == Tablet) {
        const double nx = px_ + e.dx, ny = py_ + e.dy;
        const double x = ax_ + nx, y = ay_ + ny;
        const double reach = kTrackpadPanRestart * std::min(v.width, v.height);
        if (x < 0 || y < 0 || x >= v.width || y >= v.height || hypot(nx, ny) > reach) {
            // Off the picture the game finds no ground to hold: the gesture
            // starts again under the pointer from the camera as it is now.
            out->push_back({2, ax_ + px_, ay_ + py_, zoom_, turn_});
            open_ = false;
            begin(v, out);
            px_ = e.dx;
            py_ = e.dy;
        } else {
            px_ = nx;
            py_ = ny;
        }
    } else {
        zoom_ *= exp2(-height_up(e) / points_per_doubling_);
    }
    move(out);
}

// A list's scroll: the points its content moves, at the pointer.
void TrackpadMapper::list_scroll(const TrackpadEvent &e, const TrackpadView &v,
                                 std::vector<TrackpadAction> *out) {
    if (e.dy == 0)
        return;
    TrackpadAction a;
    a.x = v.cursor_x;
    a.y = v.cursor_y;
    a.scroll = e.dy;
    out->push_back(a);
}

bool TrackpadMapper::event(const TrackpadEvent &e, const TrackpadView &v,
                           std::vector<TrackpadAction> *out) {
    const bool ends = e.phase == TrackpadEvent::Ended || e.phase == TrackpadEvent::Cancelled;
    if (e.kind == TrackpadEvent::Scroll) {
        if (!e.precise)
            return false; // a notched wheel: the game's own steps
        if (scroll_.owner == List && (e.momentum || e.phase == TrackpadEvent::Changed ||
                                      e.phase == TrackpadEvent::Ended)) {
            list_scroll(e, v, out);
            return true;
        }
        if (e.momentum) {
            // The inertia belongs to whoever took the fingers' scroll.
            if (scroll_.owner != Camera)
                return false;
            scroll_.live = !ends;
            scroll(e, v, out);
            end_if_idle(out);
            return true;
        }
        switch (e.phase) {
        case TrackpadEvent::Began:
            scroll_.owner = claim(v, mode_ != Off, true);
            // Over the interface (no map or battle under it, which keep the
            // game's own wheel when they are not the camera's): the list's.
            if (scroll_.owner == Game && !v.world && !v.zoom_view) {
                scroll_.owner = List;
                list_scroll(e, v, out);
                return true;
            }
            if (scroll_.owner != Camera)
                return false;
            if (!open_)
                anchor_cursor_ = mode_ == Tablet || (!v.world && v.zoom_view);
            scroll_.live = true;
            scroll(e, v, out);
            return true;
        case TrackpadEvent::Changed:
        case TrackpadEvent::Ended:
        case TrackpadEvent::Cancelled:
            if (scroll_.owner != Camera)
                return false;
            scroll_.live = !ends;
            scroll(e, v, out);
            end_if_idle(out);
            return true;
        default: // a touch that may start a scroll, or no phases at all
            return (scroll_.owner == Camera && open_) || scroll_.owner == List;
        }
    }
    if (mode_ != Tablet)
        return false;
    Stream &s = e.kind == TrackpadEvent::Magnify ? magnify_ : rotate_;
    if (e.phase == TrackpadEvent::Began) {
        const bool joins = open_;
        s.owner = claim(v, true, e.kind == TrackpadEvent::Magnify);
        if (s.owner != Camera)
            return false;
        if (!joins)
            anchor_cursor_ = true; // Tablet mode: under the pointer
    } else if (s.owner != Camera) {
        return false;
    }
    if (e.phase == TrackpadEvent::None || e.phase == TrackpadEvent::MayBegin)
        return true;
    s.live = !ends;
    if (!open_)
        begin(v, out);
    if (e.kind == TrackpadEvent::Magnify) {
        // Fingers apart make the picture larger: the camera comes nearer.
        const double scale = std::max(0.05, 1.0 + e.amount);
        zoom_ = invert_zoom_ ? zoom_ * scale : zoom_ / scale;
    } else {
        const double turn = e.amount * kPi / 180.0;
        turn_ += invert_rotate_ ? -turn : turn;
    }
    move(out);
    end_if_idle(out);
    return true;
}
