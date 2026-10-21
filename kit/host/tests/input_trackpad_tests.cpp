// input_trackpad_tests.cpp - the trackpad's camera gestures (input_trackpad.h).
#include "../input_trackpad.h"

#include <math.h>
#include <stdio.h>

static int g_failures = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

static bool near(double a, double b) {
    return fabs(a - b) < 1e-6;
}

static TrackpadView map_view(double x = 300, double y = 200) {
    TrackpadView v;
    v.cursor_x = x;
    v.cursor_y = y;
    v.width = 1000;
    v.height = 800;
    v.world = true;
    return v;
}

static TrackpadEvent scroll(TrackpadEvent::Phase phase, double dx, double dy, bool momentum = false) {
    TrackpadEvent e;
    e.kind = TrackpadEvent::Scroll;
    e.phase = phase;
    e.momentum = momentum;
    e.precise = true;
    e.dx = dx;
    e.dy = dy;
    return e;
}

static TrackpadEvent gesture(TrackpadEvent::Kind kind, TrackpadEvent::Phase phase, double amount) {
    TrackpadEvent e;
    e.kind = kind;
    e.phase = phase;
    e.amount = amount;
    return e;
}

static void test_height_scroll_changes_only_the_distance_around_the_middle() {
    TrackpadMapper m;
    std::vector<TrackpadAction> out;
    CHECK(m.event(scroll(TrackpadEvent::Began, 5, -kTrackpadPointsPerDoubling), map_view(), &out));
    // The middle of the window, not the pointer; a wheel turned toward the
    // player (content up) is farther.
    CHECK(out.size() == 2 && out[0].phase == 0 && near(out[0].x, 500) && near(out[0].y, 400));
    CHECK(out[1].phase == 1 && near(out[1].x, 500) && near(out[1].y, 400) && near(out[1].zoom, 2.0) &&
          near(out[1].turn, 0));
    out.clear();
    // Natural scrolling flips the deltas: fingers moving up give content up,
    // which is the wheel turned away - nearer, back to the start.
    TrackpadEvent natural = scroll(TrackpadEvent::Changed, 0, -kTrackpadPointsPerDoubling);
    natural.inverted = true;
    CHECK(m.event(natural, map_view(), &out));
    CHECK(out.size() == 1 && near(out[0].zoom, 1.0));
    out.clear();
    CHECK(m.event(scroll(TrackpadEvent::Ended, 0, 0), map_view(), &out));
    CHECK(out.size() == 2 && out[1].phase == 2 && !m.active());
}

static void test_inertia_follows_its_scroll() {
    TrackpadMapper m;
    std::vector<TrackpadAction> out;
    m.event(scroll(TrackpadEvent::Began, 0, -10), map_view(), &out);
    m.event(scroll(TrackpadEvent::Ended, 0, 0), map_view(), &out);
    out.clear();
    // A new gesture for the inertia, from the camera as it is now.
    CHECK(m.event(scroll(TrackpadEvent::Began, 0, -kTrackpadPointsPerDoubling, true), map_view(), &out));
    CHECK(out.size() == 2 && out[0].phase == 0 && near(out[1].zoom, 2.0));
    out.clear();
    CHECK(m.event(scroll(TrackpadEvent::Ended, 0, 0, true), map_view(), &out));
    CHECK(!m.active());
    // A scroll the game took keeps its inertia too (Off mode over the map).
    TrackpadMapper off;
    off.set_mode(TrackpadMapper::Off);
    out.clear();
    CHECK(!off.event(scroll(TrackpadEvent::Began, 0, -10), map_view(), &out));
    CHECK(!off.event(scroll(TrackpadEvent::Ended, 0, 0), map_view(), &out));
    CHECK(!off.event(scroll(TrackpadEvent::Began, 0, -10, true), map_view(), &out));
    CHECK(out.empty());
}

static void test_notched_wheels_stay_the_games() {
    TrackpadMapper m;
    std::vector<TrackpadAction> out;
    TrackpadEvent wheel = scroll(TrackpadEvent::None, 0, 1);
    wheel.precise = false;
    CHECK(!m.event(wheel, map_view(), &out));
    TrackpadView menu = map_view();
    menu.world = false;
    CHECK(!m.event(wheel, menu, &out));
    CHECK(out.empty());
    m.set_mode(TrackpadMapper::Off);
    CHECK(!m.event(scroll(TrackpadEvent::Began, 0, 3), map_view(), &out)); // Off: the map's wheel
    CHECK(out.empty());
}

static void test_interface_scroll_goes_to_the_list() {
    TrackpadMapper m;
    std::vector<TrackpadAction> out;
    TrackpadView menu = map_view(40, 50);
    menu.world = false;
    // A list under the fingers: the points its content moves, at the pointer.
    CHECK(m.event(scroll(TrackpadEvent::Began, 0, 12), menu, &out));
    CHECK(out.size() == 1 && near(out[0].scroll, 12) && near(out[0].x, 40));
    CHECK(m.event(scroll(TrackpadEvent::Changed, 0, -3.5), map_view(), &out)); // decided at its start
    CHECK(out.size() == 2 && near(out[1].scroll, -3.5) && near(out[1].x, 300));
    CHECK(m.event(scroll(TrackpadEvent::Ended, 0, 0), menu, &out));
    CHECK(out.size() == 2);
    // Its inertia goes on to the list.
    CHECK(m.event(scroll(TrackpadEvent::Began, 0, -50, true), menu, &out));
    CHECK(out.size() == 3 && near(out[2].scroll, -50));
    CHECK(m.event(scroll(TrackpadEvent::Ended, 0, 0, true), menu, &out));
    CHECK(!m.active());
    // The same in Off mode.
    TrackpadMapper off;
    off.set_mode(TrackpadMapper::Off);
    out.clear();
    CHECK(off.event(scroll(TrackpadEvent::Began, 0, -25), menu, &out));
    CHECK(out.size() == 1 && near(out[0].scroll, -25));
}

static void test_height_mode_leaves_pinch_and_turn() {
    TrackpadMapper m;
    std::vector<TrackpadAction> out;
    CHECK(!m.event(gesture(TrackpadEvent::Magnify, TrackpadEvent::Began, 0.1), map_view(), &out));
    CHECK(!m.event(gesture(TrackpadEvent::Rotate, TrackpadEvent::Began, 10), map_view(), &out));
    CHECK(out.empty());
}

static void test_tablet_scroll_drags_the_ground_under_the_pointer() {
    TrackpadMapper m;
    m.set_mode(TrackpadMapper::Tablet);
    std::vector<TrackpadAction> out;
    CHECK(m.event(scroll(TrackpadEvent::Began, 10, 20), map_view(), &out));
    CHECK(out.size() == 2 && near(out[0].x, 300) && near(out[0].y, 200));
    CHECK(near(out[1].x, 310) && near(out[1].y, 220) && near(out[1].zoom, 1));
    out.clear();
    // Far enough off: ends there and begins again under the pointer.
    CHECK(m.event(scroll(TrackpadEvent::Changed, 0, 300), map_view(), &out));
    CHECK(out.size() == 3 && out[0].phase == 2 && near(out[0].y, 220));
    CHECK(out[1].phase == 0 && near(out[1].x, 300) && near(out[1].y, 200));
    CHECK(out[2].phase == 1 && near(out[2].y, 500));
    // Off the window's top edge.
    TrackpadMapper top;
    top.set_mode(TrackpadMapper::Tablet);
    out.clear();
    CHECK(top.event(scroll(TrackpadEvent::Began, 0, -40), map_view(300, 50), &out));
    CHECK(out.size() == 2 && near(out[1].y, 10));
    out.clear();
    CHECK(top.event(scroll(TrackpadEvent::Changed, 0, -20), map_view(300, 50), &out));
    CHECK(out.size() == 3 && out[0].phase == 2 && out[1].phase == 0 && near(out[2].y, 30));
}

static void test_tablet_pinch_and_turn_join_the_scroll() {
    TrackpadMapper m;
    m.set_mode(TrackpadMapper::Tablet);
    std::vector<TrackpadAction> out;
    m.event(scroll(TrackpadEvent::Began, 4, 0), map_view(), &out);
    out.clear();
    TrackpadView covered = map_view();
    covered.world = false; // the pointer shows a button by now
    CHECK(m.event(gesture(TrackpadEvent::Magnify, TrackpadEvent::Began, 1.0), covered, &out));
    CHECK(out.size() == 1 && near(out[0].zoom, 0.5) && near(out[0].x, 304));
    CHECK(m.event(gesture(TrackpadEvent::Rotate, TrackpadEvent::Began, 90), covered, &out));
    CHECK(out.size() == 2 && near(out[1].turn, M_PI / 2));
    m.event(scroll(TrackpadEvent::Ended, 0, 0), map_view(), &out);
    m.event(gesture(TrackpadEvent::Magnify, TrackpadEvent::Ended, 0), map_view(), &out);
    CHECK(m.active()); // the turn still runs
    out.clear();
    m.event(gesture(TrackpadEvent::Rotate, TrackpadEvent::Ended, 0), map_view(), &out);
    CHECK(!m.active() && out.back().phase == 2);
}

static void test_focus_loss_ends_the_gesture() {
    TrackpadMapper m;
    m.set_mode(TrackpadMapper::Tablet);
    std::vector<TrackpadAction> out;
    m.event(gesture(TrackpadEvent::Magnify, TrackpadEvent::Began, 0.2), map_view(), &out);
    out.clear();
    m.cancel(&out);
    CHECK(out.size() == 1 && out[0].phase == 2 && !m.active());
    // What was left of it is no longer the camera's.
    CHECK(!m.event(gesture(TrackpadEvent::Magnify, TrackpadEvent::Changed, 0.2), map_view(), &out));
}

static void test_battle_zooms_and_pans_without_turning() {
    TrackpadView battle = map_view();
    battle.world = false;
    battle.zoom_view = true;
    std::vector<TrackpadAction> out;
    TrackpadMapper height;
    CHECK(height.event(scroll(TrackpadEvent::Began, 0, -kTrackpadPointsPerDoubling), battle, &out));
    // A battle zooms to the pointer, not the middle of the picture.
    CHECK(out.size() == 2 && near(out[0].x, 300) && near(out[0].y, 200) && near(out[1].zoom, 2.0));
    height.event(scroll(TrackpadEvent::Ended, 0, 0), battle, &out);
    out.clear();
    // Its inertia keeps the pointer as the anchor (the view is not asked again).
    CHECK(height.event(scroll(TrackpadEvent::Began, 0, -10, true), map_view(), &out));
    CHECK(out.size() == 2 && out[0].phase == 0 && near(out[0].x, 300) && near(out[0].y, 200));
    height.event(scroll(TrackpadEvent::Ended, 0, 0, true), map_view(), &out);
    // The map keeps zooming around the middle.
    out.clear();
    CHECK(height.event(scroll(TrackpadEvent::Began, 0, -10), map_view(), &out));
    CHECK(out.size() == 2 && near(out[0].x, 500) && near(out[0].y, 400));
    height.event(scroll(TrackpadEvent::Ended, 0, 0), map_view(), &out);
    TrackpadMapper tablet;
    tablet.set_mode(TrackpadMapper::Tablet);
    out.clear();
    // A turn stays the game's (the battle's camera does not turn here).
    CHECK(!tablet.event(gesture(TrackpadEvent::Rotate, TrackpadEvent::Began, 10), battle, &out));
    CHECK(out.empty());
    CHECK(tablet.event(gesture(TrackpadEvent::Magnify, TrackpadEvent::Began, 1.0), battle, &out));
    CHECK(out.size() == 2 && near(out[1].zoom, 0.5));
    tablet.event(gesture(TrackpadEvent::Magnify, TrackpadEvent::Ended, 0), battle, &out);
    // A Tablet scroll pans the field: the ground under the pointer follows.
    TrackpadMapper pan;
    pan.set_mode(TrackpadMapper::Tablet);
    out.clear();
    CHECK(pan.event(scroll(TrackpadEvent::Began, 10, 20), battle, &out));
    CHECK(out.size() == 2 && near(out[0].x, 300) && near(out[0].y, 200));
    CHECK(near(out[1].x, 310) && near(out[1].y, 220) && near(out[1].zoom, 1));
}

int main() {
    test_height_scroll_changes_only_the_distance_around_the_middle();
    test_inertia_follows_its_scroll();
    test_notched_wheels_stay_the_games();
    test_interface_scroll_goes_to_the_list();
    test_height_mode_leaves_pinch_and_turn();
    test_tablet_scroll_drags_the_ground_under_the_pointer();
    test_tablet_pinch_and_turn_join_the_scroll();
    test_focus_loss_ends_the_gesture();
    test_battle_zooms_and_pans_without_turning();
    if (g_failures) {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("input_trackpad_tests: ok\n");
    return 0;
}
