#include "page_overlay.h"
#include "../runtime/mods_seam.h"

#include <vector>

namespace {
bool g_enabled = false;
// Installed once, before the guest starts; only read after that.
const HostPagePrompt *g_prompt = nullptr;
} // namespace

void host_page_set_enabled(bool enabled) {
    g_enabled = enabled;
}
bool host_page_enabled() {
    return g_enabled;
}

const void *host_page_overlay(const void *pixels, int w, int h, int bpp, int pitch,
                              const uint32_t *palette) {
    // With mods off this is not merely a frame nobody draws on: registering
    // the page's keyboard would consume F10 and, once open, every navigation
    // key, so a run with mods disabled would not deliver the input a run
    // without the foundation delivered. Nothing here happens at all.
    if (!g_enabled)
        return pixels;

    // The page's keyboard is registered here, on the first frame a host draws,
    // and nowhere else. The loader used to do it and no longer does: a host
    // that never presents has no page to drive, and every host that CAN draw
    // one reaches this function. First frame, not load time, because the
    // loader has certainly run by then - it runs before the entry point.
    static bool page_ready = false;
    if (!page_ready) {
        page_ready = true;
        mods_page_init();
    }

    if (!pixels || w <= 0 || h <= 0)
        return pixels;
    int stride = pitch > 0 ? pitch : w * (bpp / 8);
    if (stride <= 0)
        return pixels;

    // One buffer per thread. A presenter is called from the guest thread that
    // presented, and two of them presenting at once must not share storage.
    static thread_local std::vector<uint8_t> copy;
    size_t bytes = (size_t)stride * (size_t)h;
    const uint8_t *src = (const uint8_t *)pixels;
    copy.assign(src, src + bytes);
    // A no-op when the page is hidden, which is the usual case; the copy is
    // taken anyway so there is one path and not two, and so the "we never
    // write through the caller's pointer" rule holds without a condition.
    mods_page_draw(copy.data(), w, h, bpp, stride, palette);
    return copy.data();
}

bool host_page_rgba(std::vector<uint8_t> *rgba) {
    if (!rgba)
        return false;
    rgba->clear();
    if (!g_enabled || !mods_page_visible())
        return false;
    std::vector<uint16_t> pixels(640 * 480, 0);
    mods_page_draw(pixels.data(), 640, 480, 16, 640 * 2, nullptr);
    rgba->resize(640 * 480 * 4);
    for (size_t i = 0; i < pixels.size(); ++i) {
        uint16_t c = pixels[i];
        (*rgba)[4 * i] = uint8_t(((c >> 11) & 31) * 255 / 31);
        (*rgba)[4 * i + 1] = uint8_t(((c >> 5) & 63) * 255 / 63);
        (*rgba)[4 * i + 2] = uint8_t((c & 31) * 255 / 31);
        (*rgba)[4 * i + 3] = c ? 255 : 0;
    }
    return true;
}

int host_page_menu(int w, int h, uint64_t *generation, const uint8_t **pixels) {
    // A prompt takes the menu's place, mods or no mods: the guest that asked
    // is waiting for it.
    if (g_prompt && g_prompt->open())
        return g_prompt->render(w, h, generation, pixels);
    if (!g_enabled)
        return 0;
    return mods_menu_render(w, h, generation, pixels);
}

void host_page_set_prompt(const HostPagePrompt *prompt) {
    g_prompt = prompt;
}
bool host_page_prompt_open() {
    return g_prompt && g_prompt->open();
}
int host_page_prompt_render(int w, int h, uint64_t *generation, const uint8_t **pixels) {
    if (!g_prompt || !g_prompt->open())
        return 0;
    return g_prompt->render(w, h, generation, pixels);
}
bool host_page_prompt_key(uint8_t dik, bool down) {
    return g_prompt && g_prompt->key(dik, down);
}
bool host_page_prompt_text(const char *utf8) {
    if (!host_page_prompt_open() || !utf8)
        return false;
    g_prompt->text(utf8);
    return true;
}
bool host_page_prompt_pointer(float u, float v, int button, bool down, int wheel) {
    if (!host_page_prompt_open())
        return false;
    g_prompt->pointer(u, v, button, down ? 1 : 0, wheel);
    return true;
}
bool host_page_prompt_wants_text() {
    return g_prompt && g_prompt->wants_text();
}
void host_page_prompt_cancel() {
    if (g_prompt)
        g_prompt->cancel();
}
bool host_page_visible() {
    return mods_page_visible() || host_page_prompt_open();
}
