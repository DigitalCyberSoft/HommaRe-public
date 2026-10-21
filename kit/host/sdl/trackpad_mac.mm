// trackpad_mac.mm - see trackpad_mac.h.
#import <AppKit/AppKit.h>

#include "trackpad_mac.h"

namespace {

id g_monitor;
bool (*g_handler)(const TrackpadEvent &);

TrackpadEvent::Phase phase_of(NSEventPhase p) {
    if (p & NSEventPhaseBegan)
        return TrackpadEvent::Began;
    if (p & (NSEventPhaseChanged | NSEventPhaseStationary))
        return TrackpadEvent::Changed;
    if (p & NSEventPhaseEnded)
        return TrackpadEvent::Ended;
    if (p & NSEventPhaseCancelled)
        return TrackpadEvent::Cancelled;
    if (p & NSEventPhaseMayBegin)
        return TrackpadEvent::MayBegin;
    return TrackpadEvent::None;
}

// The event as the mapper sees it; false for an event it does not read.
bool decode(NSEvent *ev, TrackpadEvent *e) {
    switch (ev.type) {
    case NSEventTypeScrollWheel:
        e->kind = TrackpadEvent::Scroll;
        e->momentum = ev.momentumPhase != NSEventPhaseNone;
        e->phase = phase_of(e->momentum ? ev.momentumPhase : ev.phase);
        e->precise = ev.hasPreciseScrollingDeltas;
        e->dx = ev.scrollingDeltaX;
        e->dy = ev.scrollingDeltaY;
        e->inverted = ev.isDirectionInvertedFromDevice;
        return true;
    case NSEventTypeMagnify:
        e->kind = TrackpadEvent::Magnify;
        e->phase = phase_of(ev.phase);
        e->amount = ev.magnification;
        return true;
    case NSEventTypeRotate:
        e->kind = TrackpadEvent::Rotate;
        e->phase = phase_of(ev.phase);
        e->amount = ev.rotation;
        return true;
    default:
        return false;
    }
}

} // namespace

bool trackpad_mac_install(SDL_Window *window, bool (*handler)(const TrackpadEvent &)) {
    if (g_monitor || !window || !handler)
        return false;
    NSWindow *ns = (__bridge NSWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    if (!ns)
        return false;
    g_handler = handler;
    __weak NSWindow *ours = ns;
    const NSEventMask mask = NSEventMaskScrollWheel | NSEventMaskMagnify | NSEventMaskRotate;
    g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                      handler:^NSEvent *(NSEvent *ev) {
                                                        TrackpadEvent e;
                                                        if (ev.window != ours || !decode(ev, &e))
                                                            return ev;
                                                        return handler(e) ? nil : ev;
                                                      }];
    return g_monitor != nil;
}

void trackpad_mac_test(int kind, int phase, int momentum, double value) {
    if (!g_handler)
        return;
    CGEventRef cg = nullptr;
    if (kind <= 1) {
        const int32_t v = (int32_t)lround(value);
        cg = CGEventCreateScrollWheelEvent2(nullptr, kCGScrollEventUnitPixel, 2, kind == 0 ? v : 0,
                                            kind == 1 ? v : 0, 0);
        if (!cg)
            return;
        CGEventSetIntegerValueField(cg, kCGScrollWheelEventIsContinuous, 1);
        CGEventSetDoubleValueField(cg, kCGScrollWheelEventPointDeltaAxis1, kind == 0 ? value : 0);
        CGEventSetDoubleValueField(cg, kCGScrollWheelEventPointDeltaAxis2, kind == 1 ? value : 0);
        CGEventSetIntegerValueField(cg, kCGScrollWheelEventScrollPhase, phase);
        CGEventSetIntegerValueField(cg, kCGScrollWheelEventMomentumPhase, momentum);
    } else {
        // A gesture event carrying a zoom (HID type 8) or a rotation (5); AppKit
        // makes it a magnify or rotate event. The field numbers are CGS's.
        cg = CGEventCreate(nullptr);
        if (!cg)
            return;
        CGEventSetIntegerValueField(cg, (CGEventField)55, 29);
        CGEventSetIntegerValueField(cg, (CGEventField)110, kind == 2 ? 8 : 5);
        CGEventSetIntegerValueField(cg, (CGEventField)132, phase);
        CGEventSetDoubleValueField(cg, (CGEventField)(kind == 2 ? 113 : 114), value);
    }
    NSEvent *ev = [NSEvent eventWithCGEvent:cg];
    CFRelease(cg);
    TrackpadEvent e;
    if (ev && decode(ev, &e))
        g_handler(e);
}
