// input_trackpad.h - a Mac trackpad's two-finger scroll, pinch and turn become
// camera gestures (runtime/native_seam.h recomp_camera_gesture).
//
// A pure state machine, like input_touch.h: it sees the trackpad's events as
// plain numbers (sdl/trackpad_mac.mm reads them from AppKit) and emits the
// camera gestures the host queues. Two modes:
//  - Height (the default): a vertical two-finger scroll moves the camera nearer
//    or farther, smoothly, around the middle of the picture (in a battle,
//    around the pointer); nothing else;
//  - Tablet: as on an iPad, the ground under the pointer follows a two-finger
//    scroll, a pinch zooms around the pointer and a turn turns the camera.
// Only a gesture that begins over a game's 3D map (RECOMP_TOUCH_WORLD) is
// taken; a notched mouse wheel stays the game's own wheel. Over anything else
// - a list, a dialog, a town - a precise scroll (and its inertia) goes to the
// list under the pointer as the points its content moves (recomp_ui_scroll),
// so it follows the fingers; where the game takes none, the host turns it into
// the game's wheel as SDL did (kTrackpadListWheelPerPoint). That holds in
// every mode, Off too. A battlefield (RECOMP_TOUCH_ZOOM_VIEW) zooms and pans but
// does not turn: there the scroll (height or, in Tablet mode, the pan) and the
// pinch are taken, a turn is not. A town is left to the game's own wheel: its camera has no operator to
// set, and wheel steps made of a pinch were jerky there.
#pragma once
#include <stdint.h>

#include <vector>

// A swipe this long (points of scroll, inertia included) halves or doubles
// the camera's distance in Height mode.
constexpr double kTrackpadPointsPerDoubling = 320.0;
// Tablet mode: once the point a scroll drags lies this far from where it
// started (a share of the window's shorter side) or leaves the window, the
// gesture starts again under the pointer, so the point stays on the picture.
constexpr double kTrackpadPanRestart = 0.3;
// Wheel steps (120 a notch) per point of a scroll over the interface that no
// list takes: SDL's own, a tenth of a notch a point.
constexpr double kTrackpadListWheelPerPoint = 12.0;

struct TrackpadEvent {
    enum Kind { Scroll, Magnify, Rotate } kind = Scroll;
    // The fingers' phase, or the inertia's for a momentum scroll. None: a
    // device that reports no phases (a notched wheel).
    enum Phase { None, MayBegin, Began, Changed, Ended, Cancelled } phase = None;
    bool momentum = false; // a scroll's inertia after the fingers lifted
    bool precise = false;  // pixel deltas (trackpad, Magic Mouse), not wheel notches
    // Scroll: how far the content moves, in window points, y down - the system's
    // natural-scrolling choice already applied - and whether it applied a flip.
    double dx = 0, dy = 0;
    bool inverted = false;
    // Magnify: the change of scale (0.1 = a tenth larger); Rotate: degrees,
    // counterclockwise positive.
    double amount = 0;
};

// What the host knows when an event arrives: the pointer and the window in
// window points, and whether the game's 3D map or a camera that only zooms
// (a battle) is under it.
struct TrackpadView {
    double cursor_x = 0, cursor_y = 0, width = 0, height = 0;
    bool world = false, zoom_view = false;
};

// A camera gesture in window points: phase 0 begins at (x, y), 1 moves, 2 ends;
// zoom and turn are since it began (native_seam.h recomp_camera_gesture). Or,
// with `scroll`, the points a list's content moves at (x, y) (positive: down,
// its start coming into view).
struct TrackpadAction {
    int phase = 0;
    double x = 0, y = 0, zoom = 1, turn = 0;
    double scroll = 0;
};

class TrackpadMapper {
  public:
    enum Mode { Height = 0, Tablet = 1, Off = 2 };
    void set_mode(int mode) {
        mode_ = mode == Tablet ? Tablet : mode == Off ? Off : Height;
    }
    int mode() const {
        return mode_;
    }
    void set_points_per_doubling(double points) {
        if (points > 1)
            points_per_doubling_ = points;
    }
    // Directions: a smaller distance for a scroll the other way, a pinch or a
    // turn that answers the other way round.
    void set_invert(bool height, bool zoom, bool rotate) {
        invert_height_ = height;
        invert_zoom_ = zoom;
        invert_rotate_ = rotate;
    }
    // True when the event is the camera's: the caller drops it then, and
    // otherwise hands it on as it would have.
    bool event(const TrackpadEvent &e, const TrackpadView &v, std::vector<TrackpadAction> *out);
    // The window lost the focus: a gesture in progress ends where it is.
    void cancel(std::vector<TrackpadAction> *out);
    bool active() const {
        return open_;
    }

  private:
    enum Owner { Nobody, Camera, List, Game };
    struct Stream {
        Owner owner = Nobody;
        bool live = false; // fingers on it, or its inertia running
    };
    Owner claim(const TrackpadView &v, bool wanted, bool battle) const;
    void begin(const TrackpadView &v, std::vector<TrackpadAction> *out);
    void move(std::vector<TrackpadAction> *out);
    void end_if_idle(std::vector<TrackpadAction> *out);
    void scroll(const TrackpadEvent &e, const TrackpadView &v, std::vector<TrackpadAction> *out);
    void list_scroll(const TrackpadEvent &e, const TrackpadView &v,
                     std::vector<TrackpadAction> *out);
    double height_up(const TrackpadEvent &e) const;

    int mode_ = Height;
    double points_per_doubling_ = kTrackpadPointsPerDoubling;
    bool invert_height_ = false, invert_zoom_ = false, invert_rotate_ = false;
    Stream scroll_, magnify_, rotate_;
    bool open_ = false;
    // Where a gesture holds the ground: under the pointer (Tablet mode, or a
    // battle's zoom), else the middle of the picture. Chosen when a scroll or
    // pinch is taken, kept through its inertia.
    bool anchor_cursor_ = false;
    // The gesture since it (re)began: its first point, the scroll's drag from
    // there, and the zoom and turn so far.
    double ax_ = 0, ay_ = 0, px_ = 0, py_ = 0, zoom_ = 1, turn_ = 0;
};
