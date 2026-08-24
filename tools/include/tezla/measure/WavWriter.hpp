#pragma once

// Minimal 32-bit-float WAV writer, so a measurement run can leave behind
// something you can actually listen to and drag into the DAW.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace tezla::measure {

/// Writes interleaved 32-bit float samples. Returns false if the file could not
/// be opened. Channel count is taken from `channels`; each vector must be the
/// same length.
inline bool writeWav (const std::string& path,
                      const std::vector<std::vector<double>>& channels,
                      double sampleRate)
{
    if (channels.empty() || channels[0].empty())
        return false;

    const auto numChannels = static_cast<std::uint16_t> (channels.size());
    const auto numFrames   = static_cast<std::uint32_t> (channels[0].size());
    const std::uint32_t dataBytes = numFrames * numChannels * 4u;

    std::FILE* file = std::fopen (path.c_str(), "wb");
    if (file == nullptr)
        return false;

    const auto u32 = [file] (std::uint32_t v) { std::fwrite (&v, 4, 1, file); };
    const auto u16 = [file] (std::uint16_t v) { std::fwrite (&v, 2, 1, file); };

    std::fwrite ("RIFF", 1, 4, file);
    u32 (36u + dataBytes);
    std::fwrite ("WAVE", 1, 4, file);

    std::fwrite ("fmt ", 1, 4, file);
    u32 (16u);
    u16 (3);                                                     // IEEE float
    u16 (numChannels);
    u32 (static_cast<std::uint32_t> (sampleRate));
    u32 (static_cast<std::uint32_t> (sampleRate) * numChannels * 4u);
    u16 (static_cast<std::uint16_t> (numChannels * 4));
    u16 (32);

    std::fwrite ("data", 1, 4, file);
    u32 (dataBytes);

    for (std::uint32_t frame = 0; frame < numFrames; ++frame)
        for (std::uint16_t channel = 0; channel < numChannels; ++channel)
        {
            const auto value = static_cast<float> (channels[channel][frame]);
            std::fwrite (&value, 4, 1, file);
        }

    std::fclose (file);
    return true;
}

} // namespace tezla::measure
