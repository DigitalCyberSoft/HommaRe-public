// native_launcher_sdl.cpp - runs the SwiftUI launcher (native_launcher.h) from
// the SDL host: finds the window's NSWindow or UIWindow through SDL's window
// properties and keeps pumping SDL events, which on Apple platforms also runs
// the run loop the SwiftUI views live on. Builds without the Swift launcher
// get the stubs at the bottom and keep the canvas launcher.
#include "native_launcher.h"

#include <SDL3/SDL.h>
#include <stdio.h>

namespace launcher {

#if defined(RECOMP_NATIVE_LAUNCHER)

bool native_available(const std::string &config_path) {
    if (config_path.empty())
        return false;
    FILE *f = fopen(config_path.c_str(), "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

int native_run(SDL_Window *window, const std::string &config_path, const std::string &modules,
               const std::string &known_exe, const std::string &profile_dir, NativeChoice *out) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
#if defined(SDL_PLATFORM_IOS)
    void *native = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
#else
    void *native = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#endif
    if (!native || !recomp_native_launcher_begin(native, config_path.c_str(), modules.c_str(),
                                                 known_exe.c_str(), profile_dir.c_str())) {
        fprintf(stderr, "[launcher] the native launcher could not open %s\n", config_path.c_str());
        return -1;
    }
    // The host creates its window hidden and shows it once the game starts;
    // the launcher lives in that window, so it has to be up now.
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    int state = 0;
    while ((state = recomp_native_launcher_poll()) == 0) {
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 50))
            continue;
        do {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                state = -1;
        } while (state == 0 && SDL_PollEvent(&e));
        if (state)
            break;
    }
    if (state == 1 && out) {
        auto s = [](const char *p) { return std::string(p ? p : ""); };
        out->exe = s(recomp_native_launcher_exe());
        out->root = s(recomp_native_launcher_root());
        out->game = s(recomp_native_launcher_game());
        out->module = s(recomp_native_launcher_module());
    }
    recomp_native_launcher_end();
    return state == 1 ? 1 : 0;
}

#else

bool native_available(const std::string &) {
    return false;
}

int native_run(SDL_Window *, const std::string &, const std::string &, const std::string &, const std::string &,
               NativeChoice *) {
    return -1;
}

#endif

} // namespace launcher
