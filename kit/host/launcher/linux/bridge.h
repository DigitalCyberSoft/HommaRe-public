// bridge.h - what native_launcher_sdl.cpp hands the native launcher on Linux
// besides native_launcher.h's C entry points (bridge.cpp implements both).
#pragma once

namespace gpu {
class Device;
}

namespace launcher::native {

// The host's GPU device and the window's surface the launcher's frames are
// presented through (the canvas launcher's presenter); without a device,
// SDL's window surface. Set before recomp_native_launcher_begin.
void present_with(gpu::Device *device, void *native_surface);

} // namespace launcher::native
