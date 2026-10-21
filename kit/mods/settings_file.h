// settings_file.h - a launcher's access to the player's settings before the game runs.
//
// The keys are the profile's mod-settings.json keys, owner prefix included:
//   host.display/frame_limit   0 game's own pacing, 1 = 40, 2 = 60, 3 = 120 fps
//   host.display/energy_saver  0 automatic (Low Power Mode, heat, battery on iPad), 1 always, 2 never
//   host.display/idle_frames   0 automatic (iPad and saving mode), 1 on, 2 off: 30 fps after a minute idle
//   host.display/render_quality 0 automatic, 1 screen's own, 2 quality, 3 balanced, 4 performance:
//                              a touch device's 3D at a share of the screen, upscaled by MetalFX
//   host.display/metal_hud     1 Apple's Metal performance HUD over the game, from the next start
//   homm5.perf/gpu_skin        0 the game's default (on), 1 on, 2 off: animated models skinned on the GPU
// A get returns 0 and leaves *out alone when the key was never saved (the game's
// default applies). A set writes the file at once; the game reads it when it starts.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int mods_settings_file_get(const char *key, int64_t *out);
int mods_settings_file_set(const char *key, int64_t value);
// The folder the settings file is in: the profile root, whose files shadow the
// game's own (the asset overlay's first tier), so a launcher may put files there.
const char *mods_settings_file_dir(void);
#ifdef __cplusplus
}
#endif
