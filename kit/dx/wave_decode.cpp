// wave_decode.cpp - RIFF WAVE in host memory to playable PCM. See wave_decode.h.
#include "wave_decode.h"
#include <string.h>
#include <algorithm>
#include <cmath>

namespace {
uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}
uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

constexpr uint16_t TAG_PCM = 1, TAG_FLOAT = 3, TAG_IMA = 0x11, TAG_EXTENSIBLE = 0xfffe;

const int16_t kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int8_t kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

struct ImaState {
    int32_t predictor = 0, index = 0;
    int16_t next(uint8_t nibble) {
        int32_t step = kStep[index];
        int32_t diff = step >> 3;
        if (nibble & 1)
            diff += step >> 2;
        if (nibble & 2)
            diff += step >> 1;
        if (nibble & 4)
            diff += step;
        predictor += (nibble & 8) ? -diff : diff;
        predictor = std::clamp(predictor, -32768, 32767);
        index = std::clamp(index + kIndex[nibble & 15], 0, 88);
        return (int16_t)predictor;
    }
};

// Wide and float PCM to signed 16-bit, clamped so a full-scale or non-finite
// float cannot wrap.
void convert_to_16(const uint8_t *src, size_t frames, uint32_t channels, uint32_t bits, bool fp,
                   std::vector<int16_t> *out) {
    uint32_t width = bits / 8;
    out->resize(frames * channels);
    for (size_t i = 0; i < frames * channels; ++i) {
        const uint8_t *s = src + i * width;
        int32_t v = 0;
        if (fp && bits == 32) {
            float f;
            memcpy(&f, s, 4);
            if (!std::isfinite(f))
                f = 0;
            v = (int32_t)std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f);
        } else if (fp && bits == 64) {
            double d;
            memcpy(&d, s, 8);
            if (!std::isfinite(d))
                d = 0;
            v = (int32_t)std::lround(std::clamp(d, -1.0, 1.0) * 32767.0);
        } else {
            v = (int16_t)(uint16_t)(s[width - 2] | s[width - 1] << 8); // the top 16 bits
        }
        (*out)[i] = (int16_t)v;
    }
}
} // namespace

// Each block starts with a predictor and step index per channel (that header
// sample is the first output frame), then 4-byte groups of eight nibbles
// alternate between the channels, low nibble first.
bool ima_adpcm_decode(const uint8_t *data, size_t bytes, uint32_t channels, uint32_t block_align,
                      uint64_t frames, std::vector<int16_t> *out) {
    out->clear();
    if (!data || (channels != 1 && channels != 2) || block_align < 4 * channels + 4 * channels ||
        block_align % (4 * channels))
        return false;
    const uint32_t per_block = (block_align - 4 * channels) * 2 / channels + 1;
    std::vector<int16_t> block((size_t)per_block * channels);
    uint64_t produced = 0;
    for (size_t at = 0; at + 4 * channels <= bytes && produced < frames; at += block_align) {
        const uint8_t *b = data + at;
        size_t available = std::min<size_t>(block_align, bytes - at);
        ImaState st[2];
        for (uint32_t ch = 0; ch < channels; ++ch) {
            st[ch].predictor = (int16_t)le16(b + 4 * ch);
            st[ch].index = std::min<int32_t>(b[4 * ch + 2], 88);
            block[ch] = (int16_t)st[ch].predictor;
        }
        uint32_t count = 1;
        for (size_t g = 4 * channels; g + 4 * channels <= available; g += 4 * channels) {
            for (uint32_t ch = 0; ch < channels; ++ch)
                for (uint32_t k = 0; k < 8; ++k) {
                    uint8_t byte = b[g + 4 * ch + k / 2];
                    uint8_t nibble = (k & 1) ? byte >> 4 : byte & 15;
                    block[(size_t)(count + k) * channels + ch] = st[ch].next(nibble);
                }
            count += 8;
        }
        uint64_t take = std::min<uint64_t>(count, frames - produced);
        out->insert(out->end(), block.begin(), block.begin() + (ptrdiff_t)(take * channels));
        produced += take;
    }
    return !out->empty();
}

// Walk the padded chunks without trusting a length to fit the image.
bool wave_decode(const uint8_t *data, size_t bytes, DecodedWave *out) {
    if (!data || !out || bytes < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4))
        return false;
    const uint8_t *fmt = nullptr, *pcm = nullptr;
    uint32_t fmt_size = 0;
    size_t pcm_bytes = 0;
    uint64_t fact_frames = UINT64_MAX;
    size_t at = 12;
    while (bytes - at >= 8) {
        uint32_t size = le32(data + at + 4);
        size_t body = at + 8, available = bytes - body;
        if (!memcmp(data + at, "fmt ", 4)) {
            if (size < 16 || size > available)
                return false;
            fmt = data + body;
            fmt_size = size;
        } else if (!memcmp(data + at, "fact", 4) && size >= 4 && size <= available) {
            fact_frames = le32(data + body);
        } else if (!memcmp(data + at, "data", 4)) {
            pcm = data + body;
            pcm_bytes = std::min<size_t>(size, available);
            break;
        }
        uint64_t padded = (uint64_t)size + (size & 1u);
        if (padded > available)
            return false;
        at = body + (size_t)padded;
    }
    if (!fmt || !pcm || !pcm_bytes)
        return false;
    uint16_t tag = le16(fmt), channels = le16(fmt + 2), block_align = le16(fmt + 12),
             bits = le16(fmt + 14);
    uint32_t rate = le32(fmt + 4);
    if (tag == TAG_EXTENSIBLE && fmt_size >= 40)
        tag = le16(fmt + 24); // the sub-format GUID's first two bytes are its tag
    if ((channels != 1 && channels != 2) || !rate || rate > INT32_MAX)
        return false;
    DecodedWave wave;
    wave.rate = rate;
    wave.channels = channels;
    std::vector<int16_t> wide;
    if (tag == TAG_IMA && bits == 4) {
        if (!ima_adpcm_decode(pcm, pcm_bytes, channels, block_align, fact_frames, &wide))
            return false;
    } else if (tag == TAG_PCM && (bits == 8 || bits == 16)) {
        size_t frame = (size_t)channels * bits / 8;
        wave.bits = bits;
        wave.pcm.assign(pcm, pcm + pcm_bytes - pcm_bytes % frame);
        if (wave.pcm.empty())
            return false;
        *out = std::move(wave);
        return true;
    } else if ((tag == TAG_PCM && (bits == 24 || bits == 32)) ||
               (tag == TAG_FLOAT && (bits == 32 || bits == 64))) {
        size_t frame = (size_t)channels * bits / 8;
        convert_to_16(pcm, pcm_bytes / frame, channels, bits, tag == TAG_FLOAT, &wide);
    } else {
        return false;
    }
    if (wide.empty())
        return false;
    wave.bits = 16;
    wave.pcm.resize(wide.size() * 2);
    memcpy(wave.pcm.data(), wide.data(), wave.pcm.size());
    *out = std::move(wave);
    return true;
}
