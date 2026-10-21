// RIFF WAVE images held in host memory, decoded to PCM a host voice can play.
// riff.h reads a header the guest built in its own memory and accepts plain
// PCM only; sound libraries that load whole files (FMOD) also meet IMA ADPCM,
// WAVE_FORMAT_EXTENSIBLE and wide or float PCM, which this converts.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

struct DecodedWave {
    uint32_t rate = 0;
    uint16_t channels = 0; // 1 or 2
    uint16_t bits = 0;     // 8 (unsigned) or 16 (signed)
    std::vector<uint8_t> pcm;
};

// True for a RIFF WAVE image whose format this decodes. 8-bit and 16-bit PCM
// are copied unchanged; 24/32-bit and float PCM and IMA ADPCM become 16-bit.
// A truncated data chunk yields the whole frames that are present.
bool wave_decode(const uint8_t *data, size_t bytes, DecodedWave *out);

// Microsoft IMA ADPCM (format tag 0x11) blocks to interleaved 16-bit PCM.
// `frames` caps the output, as the fact chunk does for the final block.
bool ima_adpcm_decode(const uint8_t *data, size_t bytes, uint32_t channels, uint32_t block_align,
                      uint64_t frames, std::vector<int16_t> *out);
