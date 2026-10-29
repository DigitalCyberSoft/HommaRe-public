// platform_ui_ios.mm - the SDL host on iPadOS: one fullscreen Metal window, the
// game in Documents/game (imported by the launcher, from the bundle of a
// developer build or from files the player chose), the launcher's platform,
// and the app lifecycle mapped onto audio and presentation.
#include "platform_ui.h"

#include "../../platform/os.h"
#include "../../runtime/layout.h"
#include "../../runtime/native_seam.h"
#include "../audio.h"
#include "../launcher/launcher_sdl.h"
#include "../present.h"
#include "game_config.h"

// A game module (RECOMP_GAME_MODULE) is loaded into an app that has these
// classes too, and Objective-C has one class per name in a process: each
// module's copies take its game's name.
#if defined(RECOMP_GAME_MODULE)
#define RECOMP_OBJC_CAT_(a, b) a##_##b
#define RECOMP_OBJC_CAT(a, b) RECOMP_OBJC_CAT_(a, b)
#define RecompPickerDelegate RECOMP_OBJC_CAT(RecompPickerDelegate, RECOMP_GAME_SYMBOL)
#define RecompCameraBar RECOMP_OBJC_CAT(RecompCameraBar, RECOMP_GAME_SYMBOL)
#define RecompRoundButton RECOMP_OBJC_CAT(RecompRoundButton, RECOMP_GAME_SYMBOL)
#define RecompCameraPanel RECOMP_OBJC_CAT(RecompCameraPanel, RECOMP_GAME_SYMBOL)
#endif

#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <dlfcn.h>
#include <filesystem>
#include <objc/runtime.h>
#include <thread>
#include <unistd.h>

#include "../../mods/settings_file.h"
#include <fstream>
#include <stdio.h>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string bundle_dir() {
    char path[4096];
    if (os_exe_path(path, sizeof path) != 0)
        return "";
    return fs::path(path).parent_path().string();
}

std::string documents_dir() {
    NSArray *paths =
        NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    return paths.count ? std::string([paths[0] fileSystemRepresentation]) : "";
}

// Started from the home screen the app has no console, and the host's log
// went nowhere: it is kept in Library/Caches/host.log (the run before in
// host-prev.log) and still reaches a console when one is attached. Once a
// process, a game module's copy included (RECOMP_HOST_LOG_TEE marks it).
void tee_log_to_file() {
    if (getenv("RECOMP_HOST_LOG_TEE"))
        return;
    setenv("RECOMP_HOST_LOG_TEE", "1", 1);
    NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    if (!dirs.count)
        return;
    const std::string dir = [dirs[0] fileSystemRepresentation];
    const std::string path = dir + "/host.log";
    std::error_code ec;
    fs::rename(path, dir + "/host-prev.log", ec);
    FILE *file = fopen(path.c_str(), "w");
    int fds[2];
    if (!file || pipe(fds) != 0) {
        if (file)
            fclose(file);
        return;
    }
    const int console = dup(STDERR_FILENO);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    std::thread([fd = fds[0], file, console]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof buf)) > 0) {
            fwrite(buf, 1, (size_t)n, file);
            fflush(file);
            if (console >= 0)
                (void)write(console, buf, (size_t)n);
        }
    }).detach();
}

// The window's root view, for the reserved regions and safe area UIKit gives
// it. Main thread.
UIView *game_root_view() {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class])
            continue;
        for (UIWindow *w in ((UIWindowScene *)scene).windows)
            if (w.isKeyWindow && w.rootViewController.view)
                return w.rootViewController.view;
    }
    return nil;
}

} // namespace

void platform_ui_init_hints() {
    tee_log_to_file();
    // iPhones rotate freely to portrait, unless the game plays only in
    // landscape (game.toml [ios] portrait); iPads stay landscape (the
    // Info.plist keys match, so the OS never offers portrait to an iPad).
    SDL_SetHint(SDL_HINT_ORIENTATIONS,
                UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad || !RECOMP_IOS_PORTRAIT
                    ? "LandscapeLeft LandscapeRight"
                    : "LandscapeLeft LandscapeRight Portrait");
    // The mapper turns fingers into mouse and key events itself.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    // RECOMP_* switches for a device with no shell: Documents/switches.txt,
    // put there with devicectl (see tools/ios_logs.py for the container).
    recomp_env_apply_file((documents_dir() + "/switches.txt").c_str());
}

// Documents/game when it is ready. Anything else - no game yet, a bundled copy
// still to import, a wrong or partial import - is the launcher's to show.
GamePath platform_ui_resolve_game(const char *flag, std::string *error) {
    GamePath g;
    if (error)
        error->clear();
    // The app that loaded this game's module (host/sdl/main.cpp) names the
    // executable its launcher picked.
    if (flag && *flag) {
        g.exe = flag;
        g.source = GamePathSource::Flag;
        return g;
    }
    const std::string docs = documents_dir() + "/game";
    const launcher::Status st = launcher::check(launcher::spec_from_config(), docs);
    if (st.state == launcher::State::Ready) {
        g.exe = st.exe;
        g.source = GamePathSource::Saved;
    }
    return g;
}

// ---------------------------------------------------------------------------
// The launcher's iPadOS platform: the document picker for a folder or a ZIP,
// imports into Documents/game (kept out of iCloud backups), the Files app and
// Finder as another way in, and a share sheet for exported saves.
// ---------------------------------------------------------------------------
@interface RecompPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) launcher::PickDone done;
@property(nonatomic) bool scoped;
@end

namespace {
std::vector<NSURL *> g_scoped; // security-scoped URLs held for an import
NSMutableArray *g_delegates;   // picker delegates alive until they answer
UIBackgroundTaskIdentifier g_background = UIBackgroundTaskInvalid;
} // namespace

@implementation RecompPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    std::vector<launcher::Picked> picked;
    for (NSURL *url in urls) {
        if (self.scoped && [url startAccessingSecurityScopedResource])
            g_scoped.push_back(url);
        launcher::Picked p;
        p.path = url.fileSystemRepresentation;
        p.name = url.lastPathComponent.UTF8String;
        picked.push_back(p);
    }
    if (self.done)
        self.done(picked, "");
    [g_delegates removeObject:self];
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    if (self.done)
        self.done({}, "");
    [g_delegates removeObject:self];
}
@end

namespace {

UIViewController *top_controller(SDL_Window *window) {
    UIWindow *uiwindow = (__bridge UIWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    UIViewController *vc = uiwindow.rootViewController;
    while (vc.presentedViewController)
        vc = vc.presentedViewController;
    return vc;
}

class IosPlatform final : public launcher::Platform {
  public:
    explicit IosPlatform(SDL_Window *window) : window_(window) {}

    launcher::PlatformInfo info() override {
        launcher::PlatformInfo i;
        i.plays_in_place = false;
        i.can_pick_folder = true;
        i.can_pick_zip = true;
        i.can_open_folder = false;
        i.touch = true;
        i.import_root = documents_dir() + "/game";
        i.profile_dir = host_layout().profile_dir;
        i.drop_hint =
            "Or copy the game folder into this app's files with Finder (iPad connected to a Mac) "
            "or the Files app, then open the app again.";
        const fs::path bundled = fs::path(bundle_dir()) / "game";
        std::error_code ec;
        if (fs::is_regular_file(bundled / RECOMP_EXECUTABLE, ec))
            i.auto_import = bundled.string();
        return i;
    }
    void present(UIDocumentPickerViewController *picker, launcher::PickDone done, bool scoped) {
        RecompPickerDelegate *delegate = [RecompPickerDelegate new];
        delegate.done = std::move(done);
        delegate.scoped = scoped;
        if (!g_delegates)
            g_delegates = [NSMutableArray new];
        [g_delegates addObject:delegate];
        picker.delegate = delegate;
        picker.modalPresentationStyle = UIModalPresentationFormSheet;
        [top_controller(window_) presentViewController:picker animated:YES completion:nil];
    }
    void pick_folder(launcher::PickDone done) override {
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]];
        present(picker, std::move(done), true);
    }
    void pick_zip(launcher::PickDone done) override {
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeZIP ]];
        present(picker, std::move(done), true);
    }
    void pick_saves(launcher::PickDone done) override {
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeZIP ]
                                                                        asCopy:YES];
        present(picker, std::move(done), false);
    }
    // Written to a temporary file first; export_ready offers it to the player.
    void pick_export(const std::string &suggested, launcher::PickDone done) override {
        launcher::Picked p;
        p.path = std::string(NSTemporaryDirectory().fileSystemRepresentation) + "/" + suggested;
        p.name = suggested;
        done({p}, "");
    }
    void export_ready(const launcher::Picked &p) override {
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:p.path.c_str()]];
        UIDocumentPickerViewController *picker =
            [[UIDocumentPickerViewController alloc] initForExportingURLs:@[ url ] asCopy:YES];
        present(picker, nullptr, false);
    }
    std::vector<std::string> candidates(const launcher::Spec &spec) override {
        // A game folder the player copied into Documents under any name.
        std::vector<std::string> out;
        const std::string docs = documents_dir();
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(docs, ec)) {
            const std::string name = entry.path().filename().string();
            if (!entry.is_directory(ec) || name == "game" || name[0] == '.')
                continue;
            // The folder itself, so a move can remove all of it afterwards.
            if (!launcher::find_root(spec, entry.path().string()).empty())
                out.push_back(entry.path().string());
        }
        return out;
    }
    // A folder the player copied into Documents is moved into Documents/game.
    bool movable(const launcher::Picked &p) override {
        const std::string docs = documents_dir() + "/";
        return !p.path.empty() && p.path.compare(0, docs.size(), docs) == 0 &&
               p.path.find('/', docs.size()) == std::string::npos && p.path != docs + "game";
    }
    void open_url(const std::string &url) override {
        NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (u)
            [[UIApplication sharedApplication] openURL:u options:@{} completionHandler:nil];
    }
    void import_activity(bool active, const launcher::Progress *progress) override {
        if (progress)
            return;
        dispatch_block_t update = ^{
          [UIApplication sharedApplication].idleTimerDisabled = active;
          if (active && g_background == UIBackgroundTaskInvalid)
              g_background = [[UIApplication sharedApplication]
                  beginBackgroundTaskWithName:@"Import game"
                            expirationHandler:^{
                              [[UIApplication sharedApplication] endBackgroundTask:g_background];
                              g_background = UIBackgroundTaskInvalid;
                            }];
          else if (!active && g_background != UIBackgroundTaskInvalid) {
              [[UIApplication sharedApplication] endBackgroundTask:g_background];
              g_background = UIBackgroundTaskInvalid;
          }
        };
        if ([NSThread isMainThread])
            update();
        else
            dispatch_async(dispatch_get_main_queue(), update);
    }
    void protect_import(const std::string &root) override {
        NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:root.c_str()]
                                isDirectory:YES];
        NSError *err = nil;
        if (![url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&err])
            fprintf(stderr, "[ios] could not exclude %s from backup: %s\n", root.c_str(),
                    err.localizedDescription.UTF8String);
    }
    void release(const launcher::Picked &p) override {
        for (auto it = g_scoped.begin(); it != g_scoped.end(); ++it)
            if (p.path == (*it).fileSystemRepresentation) {
                [*it stopAccessingSecurityScopedResource];
                g_scoped.erase(it);
                return;
            }
    }

  private:
    SDL_Window *window_;
};

} // namespace

namespace launcher {
std::unique_ptr<Platform> make_platform(SDL_Window *window) {
    return std::make_unique<IosPlatform>(window);
}
} // namespace launcher

namespace {
// SDL3 does not queue the app lifecycle events; it hands them to event
// watchers on the UIKit callstack that raised them (SDL_SendAppEvent). The
// host's event loop therefore never sees them, and this watcher is the only
// place the background transition can be acted on before iOS freezes the
// process.
bool lifecycle_watch(void *, SDL_Event *e) {
    platform_ui_handle_lifecycle(*e);
    return true;
}
} // namespace

SDL_Window *platform_ui_create_window(const char *title, int, int, int, int,
                                      SDL_WindowFlags surface_flag, int *window_mode) {
    static bool watching = false;
    if (!watching) {
        SDL_AddEventWatch(lifecycle_watch, nullptr);
        watching = true;
    }
    SDL_Window *w = SDL_CreateWindow(
        title, 0, 0, surface_flag | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window_mode)
        *window_mode = 2;
    // No vertical bar (iOS 27.1: a foldable iPhone puts the status bar, hidden
    // or not, in a column along one side, and keeps that side's safe area for
    // it): the game is a full-screen picture, as Apple's guidance for a
    // non-scrolling layout says. Added to the window's own view controller
    // class (SDL's, a game module's copy of it), which does not define it.
    if (@available(iOS 27.1, *)) {
        UIWindow *uiwindow = w ? (__bridge UIWindow *)SDL_GetPointerProperty(
                                     SDL_GetWindowProperties(w), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr)
                               : nil;
        UIViewController *vc = uiwindow.rootViewController;
        if (vc) {
            IMP disabled = imp_implementationWithBlock(^NSInteger(id) {
              return UIVerticalBarBehaviorDisabled;
            });
            const bool added = class_addMethod(vc.class, @selector(preferredVerticalBarBehavior), disabled, "q@:");
            [vc setNeedsUpdateOfVerticalBarConfiguration];
            fprintf(stderr, "[ios] vertical bar %s\n", added ? "disabled" : "left as the view controller has it");
        }
    }
    return w;
}

// The game may be closed from the app switcher without reaching its own exit,
// so it saves now (runtime/native_seam.h recomp_app_background). Its own
// thread does that in its next frame, which may need this (main) thread, so
// the wait runs on a background queue under a background task that keeps the
// process alive until it is over.
static void save_before_background() {
    UIApplication *app = UIApplication.sharedApplication;
    __block UIBackgroundTaskIdentifier task = UIBackgroundTaskInvalid;
    task = [app beginBackgroundTaskWithName:@"save settings"
                          expirationHandler:^{
                            [app endBackgroundTask:task];
                            task = UIBackgroundTaskInvalid;
                          }];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      recomp_app_background();
      dispatch_async(dispatch_get_main_queue(), ^{
        if (task != UIBackgroundTaskInvalid) {
            [app endBackgroundTask:task];
            task = UIBackgroundTaskInvalid;
        }
      });
    });
}

bool platform_ui_handle_lifecycle(const SDL_Event &e) {
    switch (e.type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        fprintf(stderr, "[ios] will enter background: suspending presentation and audio\n");
        save_before_background(); // the game may never see its own exit
        host_present_suspend(true);
        host_audio_pause(true);
        return true;
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        fprintf(stderr, "[ios] did enter background\n");
        host_present_suspend(true);
        host_audio_pause(true);
        return true;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
        fprintf(stderr, "[ios] will enter foreground\n");
        return true;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        fprintf(stderr, "[ios] did enter foreground: resuming audio and presentation\n");
        host_audio_pause(false);
        host_present_suspend(false);
        return true;
    case SDL_EVENT_TERMINATING:
        fprintf(stderr, "[ios] terminating\n");
        host_present_suspend(true);
        host_audio_pause(true);
        return true;
    default:
        return false;
    }
}

bool platform_ui_keypad_wanted() {
    return !SDL_HasKeyboard();
}

bool platform_ui_touch_device() {
    return true;
}

bool platform_ui_phone() {
    return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone;
}

int platform_ui_cutout_side() {
    if (UIDevice.currentDevice.userInterfaceIdiom != UIUserInterfaceIdiomPhone)
        return 0;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class])
            continue;
        switch (((UIWindowScene *)scene).effectiveGeometry.interfaceOrientation) {
        case UIInterfaceOrientationLandscapeRight: // the home side on the right:
            return -1;                             // the top, with the housing, on the left
        case UIInterfaceOrientationLandscapeLeft:
            return 1;
        default:
            return 0;
        }
    }
    return 0;
}

static bool cutout_in_corner_now(int w_pt, int h_pt);

// Asked every turn: the answer is kept for the window's size, and asked again
// now and then (UIKit's regions may come after the first layout).
bool platform_ui_cutout_in_corner(int w_pt, int h_pt) {
    static int seen_w = -1, seen_h = -1, turns = 0;
    static bool answer = false;
    if (w_pt != seen_w || h_pt != seen_h || ++turns >= 120) {
        seen_w = w_pt;
        seen_h = h_pt;
        turns = 0;
        answer = cutout_in_corner_now(w_pt, h_pt);
    }
    return answer;
}

static bool cutout_in_corner_now(int w_pt, int h_pt) {
    if (const char *v = getenv("RECOMP_CUTOUT_IN_CORNER"); v && *v)
        return v[0] != '0';
    // What UIKit says is occluded (iOS 27.1 reserved regions): a camera in the
    // top or bottom part of a side is a corner's.
    if (@available(iOS 27.1, *)) {
        if (UIView *v = game_root_view()) {
            NSArray<UIViewReservedRegion *> *regions =
                [v reservedRegionsOfKind:UIViewReservedRegionKind.occlusionRegionKind
                                 options:UIViewReservedRegionQueryOptionsIncludeInactive];
            static NSUInteger told = NSUIntegerMax;
            if (regions.count != told) {
                told = regions.count;
                for (UIViewReservedRegion *r in regions)
                    fprintf(stderr, "[ios] occluded region %.0f,%.0f %.0fx%.0f in %.0fx%.0f%s\n", r.frame.origin.x,
                            r.frame.origin.y, r.frame.size.width, r.frame.size.height, v.bounds.size.width,
                            v.bounds.size.height, r.isActive ? "" : " (inactive)");
                if (!regions.count)
                    fprintf(stderr, "[ios] no occluded regions reported\n");
            }
            const CGSize b = v.bounds.size;
            bool corner = false, middle = false;
            for (UIViewReservedRegion *r in regions) {
                const CGRect f = r.frame;
                const bool side = f.origin.x <= 1 || CGRectGetMaxX(f) >= b.width - 1;
                if (!side || b.height <= 0)
                    continue;
                const double cy = CGRectGetMidY(f) / b.height;
                (cy < 0.3 || cy > 0.7 ? corner : middle) = true;
            }
            if (corner || middle)
                return corner && !middle;
        }
    }
    // Otherwise by the screen's shape: a Dynamic Island in the middle of a
    // side is a long phone's (19.5:9); a shorter screen's camera (a foldable's
    // closed one, nearly 3:2) is a hole in a corner.
    const int lo = std::min(w_pt, h_pt), hi = std::max(w_pt, h_pt);
    return lo > 0 && hi < 1.8 * lo;
}

bool platform_ui_pointer_capture_supported() {
    return false;
}

int platform_ui_default_overlay() {
    return 0;
}

void platform_ui_process_exit(int code) {
    // UIApplicationMain never returns, so the game's own Exit would leave its
    // last frame on the screen; the game asked to end, and this ends it.
    fprintf(stderr, "[ios] the game exited (%d); ending the app\n", code);
    fflush(stderr);
    exit(code);
}

// ---------------------------------------------------------------------------
// Haptics: a light impact tick for on-screen control presses, and a Core
// Haptics continuous player standing in for game rumble on a device with no
// controller. Both must run on the main thread.
// ---------------------------------------------------------------------------
namespace {
UIImpactFeedbackGenerator *g_tap_generator;        // prepared once, reused for every tap
CHHapticEngine *g_haptic_engine;                   // created once, started lazily
id<CHHapticAdvancedPatternPlayer> g_rumble_player; // the current 30 s rumble, or nil

bool device_supports_haptics() {
    static const bool supported = CHHapticEngine.capabilitiesForHardware.supportsHaptics;
    return supported;
}

// The engine, created on first use. A stop or reset (background, audio
// session interruption) drops the player, since it dies with the engine.
CHHapticEngine *haptic_engine() {
    if (!g_haptic_engine) {
        NSError *error = nil;
        g_haptic_engine = [[CHHapticEngine alloc] initAndReturnError:&error];
        if (error) {
            fprintf(stderr, "[ios] CHHapticEngine init failed: %s\n",
                    error.localizedDescription.UTF8String);
            return nil;
        }
        // Core Haptics calls both handlers on its own background queue; every
        // other read/write of g_rumble_player runs on the main queue (the
        // functions below), so hop there before touching it.
        g_haptic_engine.stoppedHandler = ^(CHHapticEngineStoppedReason) {
          dispatch_async(dispatch_get_main_queue(), ^{
            g_rumble_player = nil;
          });
        };
        g_haptic_engine.resetHandler = ^{
          dispatch_async(dispatch_get_main_queue(), ^{
            g_rumble_player = nil;
          });
        };
    }
    return g_haptic_engine;
}
} // namespace

void platform_ui_haptic_tap() {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!g_tap_generator) {
          g_tap_generator =
              [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
          [g_tap_generator prepare];
      }
      [g_tap_generator impactOccurred];
    });
}

void platform_ui_device_rumble(uint16_t low, uint16_t high) {
    const float intensity = std::max(low, high) / 65535.0f;
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!device_supports_haptics())
          return;
      if (intensity <= 0.0f) {
          [g_rumble_player cancelAndReturnError:nil];
          g_rumble_player = nil;
          return;
      }
      NSError *error = nil;
      if (g_rumble_player) {
          // Already rumbling: retune it in place rather than restart it.
          CHHapticDynamicParameter *param = [[CHHapticDynamicParameter alloc]
              initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                            value:intensity
                     relativeTime:0];
          [g_rumble_player sendParameters:@[ param ] atTime:CHHapticTimeImmediate error:&error];
          if (!error)
              return;
          g_rumble_player = nil; // the player died under us; fall through and rebuild it
      }
      CHHapticEngine *engine = haptic_engine();
      if (!engine)
          return;
      [engine startAndReturnError:&error];
      if (error)
          return;
      CHHapticEventParameter *intensity_param = [[CHHapticEventParameter alloc]
          initWithParameterID:CHHapticEventParameterIDHapticIntensity
                        value:intensity];
      CHHapticEventParameter *sharpness_param = [[CHHapticEventParameter alloc]
          initWithParameterID:CHHapticEventParameterIDHapticSharpness
                        value:0.3f];
      CHHapticEvent *event =
          [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                        parameters:@[ intensity_param, sharpness_param ]
                                      relativeTime:0
                                          duration:30.0];
      CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ]
                                                              parameters:@[]
                                                                   error:&error];
      if (error)
          return;
      id<CHHapticAdvancedPatternPlayer> player = [engine createAdvancedPlayerWithPattern:pattern
                                                                                   error:&error];
      if (error || !player)
          return;
      [player startAtTime:CHHapticTimeImmediate error:&error];
      if (!error)
          g_rumble_player = player;
    });
}

// ---------------------------------------------------------- camera buttons --
// Round, dark buttons with a gold rim, as the game's own map buttons, in a
// column at the top right. UIKit takes their touches, so none reaches the map.
@interface RecompCameraBar : UIStackView
@property(nonatomic) void (*press)(int);
@end

@implementation RecompCameraBar
- (instancetype)init {
    self = [super initWithFrame:CGRectZero];
    self.axis = UILayoutConstraintAxisVertical;
    self.spacing = 14;
    self.translatesAutoresizingMaskIntoConstraints = NO;
    // In RECOMP_CAMERA_* order (runtime/native_seam.h).
    NSArray<NSString *> *symbols =
        @[ @"plus", @"minus", @"arrow.counterclockwise", @"arrow.clockwise" ];
    NSArray<NSString *> *labels = @[ @"Zoom in", @"Zoom out", @"Turn left", @"Turn right" ];
    UIImageSymbolConfiguration *weight =
        [UIImageSymbolConfiguration configurationWithPointSize:24 weight:UIImageSymbolWeightBold];
    for (NSUInteger i = 0; i < symbols.count; ++i) {
        UIButton *b = [UIButton buttonWithType:UIButtonTypeCustom];
        [b setImage:[UIImage systemImageNamed:symbols[i] withConfiguration:weight]
           forState:UIControlStateNormal];
        b.tintColor = [UIColor colorWithRed:0.98 green:0.90 blue:0.66 alpha:1];
        b.backgroundColor = [UIColor colorWithRed:0.08 green:0.06 blue:0.04 alpha:0.62];
        b.layer.cornerRadius = 30;
        b.layer.borderWidth = 2.5;
        b.layer.borderColor = [UIColor colorWithRed:0.80 green:0.64 blue:0.30 alpha:0.95].CGColor;
        b.accessibilityLabel = labels[i];
        b.tag = (NSInteger)i + 1;
        [b.widthAnchor constraintEqualToConstant:60].active = YES;
        [b.heightAnchor constraintEqualToConstant:60].active = YES;
        [b addTarget:self action:@selector(down:) forControlEvents:UIControlEventTouchDown];
        [b addTarget:self
                      action:@selector(up:)
            forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside |
                             UIControlEventTouchCancel];
        [b addTarget:self action:@selector(tapped:) forControlEvents:UIControlEventTouchUpInside];
        [self addArrangedSubview:b];
    }
    return self;
}
- (void)down:(UIButton *)b {
    b.transform = CGAffineTransformMakeScale(0.9, 0.9);
    b.alpha = 0.8;
}
- (void)up:(UIButton *)b {
    b.transform = CGAffineTransformIdentity;
    b.alpha = 1;
}
- (void)tapped:(UIButton *)b {
    platform_ui_haptic_tap();
    if (self.press)
        self.press((int)b.tag);
}
@end

// The game's own art for them, when it ships it (touch-ui/camera-panel/ in the
// bundle, game.toml's repository folder): a framed column of round buttons and
// the blue corner arrow that folds the panel away, as the minimap's does.
// layout.json places each sprite on the panel's canvas, in source pixels:
// {"canvas": [w, h], "sprites": {name: {x, y, w, h, center: [x, y], radius}}}.
@interface RecompRoundButton : UIButton
@property(nonatomic) CGPoint hitCenter; // in the button's own points
@property(nonatomic) CGFloat hitRadius;
@end

@implementation RecompRoundButton
- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent *)event {
    const CGFloat dx = p.x - self.hitCenter.x, dy = p.y - self.hitCenter.y;
    return dx * dx + dy * dy <= self.hitRadius * self.hitRadius;
}
@end

@interface RecompCameraPanel : UIView
@property(nonatomic) void (*press)(int);
@property(nonatomic) CGSize size; // in points
- (instancetype)initWithFolder:(NSString *)folder phone:(BOOL)phone;
@end

@implementation RecompCameraPanel {
    UIImageView *frame_;
    NSMutableArray<UIView *> *folding_; // the frame and the camera buttons
    RecompRoundButton *arrow_;
    CGPoint fold_at_; // the arrow's centre: the panel folds into it
    BOOL collapsed_;
}
// Diameter of a camera button on screen: about the size of the game's own
// round map buttons on a 13-inch iPad; on a phone-sized window Apple's 44 pt
// minimum, so the column stays clear of the jog dial below it.
static CGFloat camera_button_pt(BOOL phone) {
    return phone ? 44 : 64;
}
static NSString *const kCollapsedKey = @"RecompCameraPanelCollapsed";

- (instancetype)initWithFolder:(NSString *)folder phone:(BOOL)phone {
    NSData *json = [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:@"layout.json"]];
    NSDictionary *layout = json ? [NSJSONSerialization JSONObjectWithData:json options:0 error:nil] : nil;
    NSDictionary *sprites = layout[@"sprites"];
    NSArray *canvas = layout[@"canvas"];
    if (![sprites isKindOfClass:NSDictionary.class] || canvas.count != 2 || !sprites[@"frame"] ||
        !sprites[@"btn-collapse"] || !sprites[@"btn-zoom-in"])
        return nil;
    UIImage *frame_image = [UIImage imageWithContentsOfFile:[folder stringByAppendingPathComponent:@"frame.png"]];
    if (!frame_image)
        return nil;
    // Sprites may reach past the canvas (the arrow sticks out at the bottom
    // left): the view covers them all, so each can be touched.
    CGFloat min_x = 0, min_y = 0, max_x = [canvas[0] doubleValue], max_y = [canvas[1] doubleValue];
    for (NSString *name in sprites) {
        NSDictionary *s = sprites[name];
        min_x = MIN(min_x, [s[@"x"] doubleValue]);
        min_y = MIN(min_y, [s[@"y"] doubleValue]);
        max_x = MAX(max_x, [s[@"x"] doubleValue] + [s[@"w"] doubleValue]);
        max_y = MAX(max_y, [s[@"y"] doubleValue] + [s[@"h"] doubleValue]);
    }
    const CGFloat scale = camera_button_pt(phone) / (2 * [sprites[@"btn-zoom-in"][@"radius"] doubleValue]);
    self = [super initWithFrame:CGRectMake(0, 0, (max_x - min_x) * scale, (max_y - min_y) * scale)];
    _size = self.bounds.size;
    self.translatesAutoresizingMaskIntoConstraints = NO;
    auto place = ^CGRect(NSDictionary *s) {
      return CGRectMake(([s[@"x"] doubleValue] - min_x) * scale, ([s[@"y"] doubleValue] - min_y) * scale,
                        [s[@"w"] doubleValue] * scale, [s[@"h"] doubleValue] * scale);
    };
    folding_ = [NSMutableArray array];
    frame_ = [[UIImageView alloc] initWithImage:frame_image];
    frame_.frame = place(sprites[@"frame"]);
    frame_.userInteractionEnabled = YES; // a touch on the frame stops here, off the map
    [self addSubview:frame_];
    [folding_ addObject:frame_];
    auto button = ^RecompRoundButton *(NSString *name, CGFloat min_radius) {
      NSDictionary *s = sprites[name];
      NSString *base = [folder stringByAppendingPathComponent:name];
      UIImage *normal = [UIImage imageWithContentsOfFile:[base stringByAppendingString:@"-normal.png"]];
      UIImage *pushed = [UIImage imageWithContentsOfFile:[base stringByAppendingString:@"-pushed.png"]];
      if (!s || !normal)
          return (RecompRoundButton *)nil;
      RecompRoundButton *b = [RecompRoundButton buttonWithType:UIButtonTypeCustom];
      b.frame = place(s);
      [b setImage:normal forState:UIControlStateNormal];
      [b setImage:pushed ?: normal forState:UIControlStateHighlighted];
      b.adjustsImageWhenHighlighted = NO;
      NSArray *c = s[@"center"];
      b.hitCenter = c.count == 2 ? CGPointMake(([c[0] doubleValue] - [s[@"x"] doubleValue]) * scale,
                                               ([c[1] doubleValue] - [s[@"y"] doubleValue]) * scale)
                                 : CGPointMake(b.bounds.size.width / 2, b.bounds.size.height / 2);
      b.hitRadius = MAX(min_radius, [s[@"radius"] doubleValue] * scale);
      [b addTarget:self action:@selector(tapped:) forControlEvents:UIControlEventTouchUpInside];
      [self addSubview:b];
      return b;
    };
    // In RECOMP_CAMERA_* order (runtime/native_seam.h).
    NSArray<NSString *> *names = @[ @"btn-zoom-in", @"btn-zoom-out", @"btn-rotate-left", @"btn-rotate-right" ];
    NSArray<NSString *> *labels = @[ @"Zoom in", @"Zoom out", @"Turn left", @"Turn right" ];
    for (NSUInteger i = 0; i < names.count; ++i) {
        RecompRoundButton *b = button(names[i], 0);
        if (!b)
            return nil;
        b.tag = (NSInteger)i + 1;
        b.accessibilityLabel = labels[i];
        [folding_ addObject:b];
    }
    // The arrow is small on the art; a finger gets a 44 pt target all the same.
    arrow_ = button(@"btn-collapse", 22);
    if (!arrow_)
        return nil;
    arrow_.tag = 0;
    arrow_.accessibilityLabel = @"Hide the camera buttons";
    [self bringSubviewToFront:arrow_]; // drawn over the frame's corner
    fold_at_ = [arrow_ convertPoint:arrow_.hitCenter toView:self];
    collapsed_ = [NSUserDefaults.standardUserDefaults boolForKey:kCollapsedKey];
    [self showCollapsed:collapsed_ animated:NO];
    return self;
}

// A touch on the panel's empty corners goes on to the map.
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)event {
    UIView *v = [super hitTest:p withEvent:event];
    return v == self ? nil : v;
}

// Folded: the frame and the buttons shrink into the arrow, which turns round
// to point back out.
- (void)showCollapsed:(BOOL)collapsed animated:(BOOL)animated {
    auto apply = ^{
      for (UIView *v in folding_) {
          const CGPoint c = v.center;
          v.transform = collapsed ? CGAffineTransformConcat(
                                        CGAffineTransformMakeTranslation((fold_at_.x - c.x) * 0.85,
                                                                         (fold_at_.y - c.y) * 0.85),
                                        CGAffineTransformMakeScale(0.15, 0.15))
                                  : CGAffineTransformIdentity;
          v.alpha = collapsed ? 0 : 1;
      }
      arrow_.transform = collapsed ? CGAffineTransformMakeRotation((CGFloat)M_PI) : CGAffineTransformIdentity;
    };
    if (animated)
        [UIView animateWithDuration:0.22
                              delay:0
                            options:UIViewAnimationOptionCurveEaseInOut
                         animations:apply
                         completion:nil];
    else
        apply();
}

- (void)tapped:(UIButton *)b {
    platform_ui_haptic_tap();
    if (b.tag == 0) {
        collapsed_ = !collapsed_;
        [NSUserDefaults.standardUserDefaults setBool:collapsed_ forKey:kCollapsedKey];
        [self showCollapsed:collapsed_ animated:YES];
        return;
    }
    if (self.press)
        self.press((int)b.tag);
}
@end

// A phone's map interface is drawn larger (native/widescreen.c in the base
// game): its jog dial takes the right side's lower half, and the left keeps a
// column clear above the minimap, below the resource bar. The panel moves in
// from that edge only where the camera housing is, as the game's interface
// does (the host's safe insets), and follows a rotation. Only acts on a change.
static NSLayoutConstraint *g_cutout_lead = nil; // dropped with the bar it holds
static int g_cutout_side = 2;
static void follow_cutout(UIView *bar) {
    UIView *root = bar.superview;
    const int side = platform_ui_cutout_side();
    if (!root || side == g_cutout_side)
        return;
    g_cutout_side = side;
    g_cutout_lead.active = NO;
    g_cutout_lead = side <= 0
               ? [bar.leadingAnchor constraintEqualToAnchor:root.safeAreaLayoutGuide.leadingAnchor
                                                    constant:12]
               : [bar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:22];
    g_cutout_lead.active = YES;
}

void platform_ui_camera_buttons(SDL_Window *window, bool visible, bool phone, void (*press)(int)) {
    static UIView *bar = nil;
    static bool shown = false;
    static bool built_phone = false;
    // A window that turned phone-sized or tablet-sized (a foldable folded or
    // opened): the buttons come again at the other size and place.
    if (bar && phone != built_phone) {
        [bar removeFromSuperview];
        bar = nil;
        g_cutout_lead = nil;
        g_cutout_side = 2;
        shown = false; // built again below when it is wanted
    }
    if (phone && bar)
        follow_cutout(bar);
    if (visible == shown && (bar || !visible))
        return;
    shown = visible;
    if (!bar) {
        built_phone = phone;
        UIWindow *uiwindow = (__bridge UIWindow *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
        UIView *root = uiwindow.rootViewController.view;
        if (!root)
            return;
        NSString *art = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"touch-ui/camera-panel"];
        RecompCameraPanel *panel = [[RecompCameraPanel alloc] initWithFolder:art phone:phone];
        if (panel) {
            [panel.widthAnchor constraintEqualToConstant:panel.size.width].active = YES;
            [panel.heightAnchor constraintEqualToConstant:panel.size.height].active = YES;
            bar = panel;
        } else {
            bar = [[RecompCameraBar alloc] init]; // no art: plain buttons
        }
        bar.alpha = 0;
        [root addSubview:bar];
        UILayoutGuide *safe = root.safeAreaLayoutGuide;
        if (!phone)
            [bar.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-12].active = YES;
        [bar.topAnchor constraintEqualToAnchor:safe.topAnchor constant:phone ? 44 : panel ? 56 : 72].active = YES;
    }
    if (phone)
        follow_cutout(bar);
    if ([bar isKindOfClass:RecompCameraPanel.class])
        ((RecompCameraPanel *)bar).press = press;
    else
        ((RecompCameraBar *)bar).press = press;
    bar.userInteractionEnabled = visible;
    [UIView animateWithDuration:0.18
                     animations:^{
                       bar.alpha = visible ? 1 : 0;
                     }];
}

// ------------------------------------------------------------ split ring --
// A gold ring around a finger that rests while dragging a stack: it fills as
// the rest counts down to a split, then holds full (Shift is down) with a
// divide sign. Drawn over the game; it takes no touches.
@interface RecompSplitRing : UIView
@property(nonatomic) BOOL held;
- (void)setProgress:(double)progress;
@end

@implementation RecompSplitRing {
    CAShapeLayer *track_, *fill_;
    UIImageView *sign_;
}
- (instancetype)init {
    const CGFloat d = 96;
    self = [super initWithFrame:CGRectMake(0, 0, d, d)];
    self.userInteractionEnabled = NO;
    UIBezierPath *circle = [UIBezierPath bezierPathWithArcCenter:CGPointMake(d / 2, d / 2)
                                                          radius:d / 2 - 6
                                                      startAngle:-M_PI_2
                                                        endAngle:3 * M_PI_2
                                                       clockwise:YES];
    track_ = [CAShapeLayer layer];
    track_.path = circle.CGPath;
    track_.fillColor = UIColor.clearColor.CGColor;
    track_.strokeColor = [UIColor colorWithRed:0.08 green:0.06 blue:0.04 alpha:0.45].CGColor;
    track_.lineWidth = 7;
    [self.layer addSublayer:track_];
    fill_ = [CAShapeLayer layer];
    fill_.path = circle.CGPath;
    fill_.fillColor = UIColor.clearColor.CGColor;
    fill_.strokeColor = [UIColor colorWithRed:0.98 green:0.84 blue:0.45 alpha:0.95].CGColor;
    fill_.lineWidth = 5;
    fill_.lineCap = kCALineCapRound;
    fill_.strokeEnd = 0;
    [self.layer addSublayer:fill_];
    UIImageSymbolConfiguration *weight =
        [UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightBold];
    sign_ = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"divide.circle.fill"
                                                       withConfiguration:weight]];
    sign_.tintColor = [UIColor colorWithRed:0.98 green:0.84 blue:0.45 alpha:1];
    sign_.backgroundColor = [UIColor colorWithRed:0.08 green:0.06 blue:0.04 alpha:0.8];
    sign_.layer.cornerRadius = 11;
    sign_.frame = CGRectMake(d - 24, 2, 22, 22);
    sign_.contentMode = UIViewContentModeCenter;
    sign_.alpha = 0;
    [self addSubview:sign_];
    return self;
}
- (void)setProgress:(double)progress {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    fill_.strokeEnd = progress;
    [CATransaction commit];
    const BOOL held = progress >= 1;
    if (held == _held)
        return;
    _held = held;
    sign_.alpha = held ? 1 : 0;
    if (held) {
        self.transform = CGAffineTransformMakeScale(1.12, 1.12);
        [UIView animateWithDuration:0.18
                         animations:^{
                           self.transform = CGAffineTransformIdentity;
                         }];
    }
}
@end

void platform_ui_split_ring(SDL_Window *window, bool visible, double x, double y, double progress) {
    static RecompSplitRing *ring = nil;
    if (!visible) {
        if (ring && !ring.hidden) {
            ring.hidden = YES;
            [ring setProgress:0];
        }
        return;
    }
    if (!ring) {
        UIWindow *uiwindow = (__bridge UIWindow *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
        UIView *root = uiwindow.rootViewController.view;
        if (!root)
            return;
        ring = [[RecompSplitRing alloc] init];
        [root addSubview:ring];
    }
    [ring.superview bringSubviewToFront:ring];
    ring.hidden = NO;
    ring.center = CGPointMake(x, y);
    const BOOL was_held = ring.held;
    [ring setProgress:progress];
    if (ring.held && !was_held)
        platform_ui_haptic_tap();
}

#if defined(RECOMP_NATIVE_SCREENS)
// The game's SwiftUI screens (native-screens/ in the game repository, built
// into the RecompScreens module): NativeScreens.swift's C entry points.
using ScreensShow = void (*)(void *, const char *, void (*)(const char *));
using ScreensHide = void (*)(void);
#if defined(RECOMP_GAME_MODULE)
// A game module (an expansion in the multi-game app) carries no copy of the
// module: the app's own serves it. Two copies in one process register the
// same Objective-C classes twice (the runtime warns, and one copy then works
// with the other's objects: a crash in Hammers of Fate's ubi-gs login). The
// screens read the game's paks themselves (RECOMP_GAME_ROOT), never the host.
static ScreensShow screens_show() {
    static ScreensShow f = (ScreensShow)dlsym(RTLD_MAIN_ONLY, "recomp_native_screens_show");
    return f;
}
static ScreensHide screens_hide() {
    static ScreensHide f = (ScreensHide)dlsym(RTLD_MAIN_ONLY, "recomp_native_screens_hide");
    return f;
}
#else
extern "C" void recomp_native_screens_show(void *root_view, const char *state,
                                           void (*command)(const char *));
extern "C" void recomp_native_screens_hide(void);
static ScreensShow screens_show() {
    return recomp_native_screens_show;
}
static ScreensHide screens_hide() {
    return recomp_native_screens_hide;
}
#endif
#endif

void platform_ui_native_screen(SDL_Window *window, const char *state, void (*command)(const char *)) {
#if defined(RECOMP_NATIVE_SCREENS)
    if (!state) {
        if (ScreensHide hide = screens_hide())
            hide();
        return;
    }
    UIWindow *uiwindow = (__bridge UIWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    UIView *root = uiwindow.rootViewController.view;
    ScreensShow show = screens_show();
    if (root && show)
        show((__bridge void *)root, state, command);
#else
    (void)window;
    (void)state;
    (void)command;
#endif
}

bool platform_ui_phone_interface() {
    // host.ui/phone_interface: 0 off, 1 by the screen (an iPhone's), 2 always
    // (an iPad too).
    int64_t on = 1;
    mods_settings_file_get("host.ui/phone_interface", &on);
    return on == 2 || (on != 0 && UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone);
}

void platform_ui_seed_profile(const std::string &profile_dir) {
    const std::string seed = host_resource("profile-seed");
    std::error_code ec;
    if (seed.empty() || profile_dir.empty() || !fs::is_directory(seed, ec))
        return;
    for (auto it = fs::recursive_directory_iterator(seed, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        fs::path rel = fs::relative(it->path(), seed, ec);
        // phone/...: on an iPhone with the launcher's phone interface not off
        // (host.ui/phone_interface: 0 off, 1 by the screen, 2 always - then on
        // an iPad too), at the path without the prefix; taken out of the
        // profile again when it is off. On a foldable iPhone they stay for
        // both screens: the game's side picks the layout by the window's size.
        if (auto first = rel.begin(); first != rel.end() && *first == "phone") {
            fs::path rest;
            for (auto part = std::next(first); part != rel.end(); ++part)
                rest /= *part;
            rel = rest;
            if (!platform_ui_phone_interface()) {
                std::error_code e2;
                if (fs::remove(fs::path(profile_dir) / rel, e2))
                    fprintf(stderr, "[ios] profile file %s: taken out (phone interface off)\n",
                            rel.c_str());
                continue;
            }
        }
        const fs::path dest = fs::path(profile_dir) / rel;
        std::error_code e2;
        if (fs::exists(dest, e2) && fs::file_size(dest, e2) == it->file_size(e2))
            continue;
        fs::create_directories(dest.parent_path(), e2);
        fs::copy_file(it->path(), dest, fs::copy_options::overwrite_existing, e2);
        fprintf(stderr, "[ios] profile file %s: %s\n", rel.c_str(),
                e2 ? e2.message().c_str() : "put in place");
    }
}
