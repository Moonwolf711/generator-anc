// wav.hpp -- minimal 16-bit PCM mono WAV writer. Narrow paths only
// (MinGW's wide-char fstream ctor is unsafe; std::fopen with a narrow path is fine).
#ifndef EOC_WAV_HPP
#define EOC_WAV_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wav {

// Read a 16-bit PCM WAV, downmix to mono, return float samples in [-1,1].
// Sets fsOut to the file's sample rate. Returns empty on failure.
inline std::vector<float> read_mono16(const std::string& path, int& fsOut) {
    std::vector<float> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;

    auto r32 = [&]() -> std::uint32_t {
        unsigned char b[4];
        if (std::fread(b, 1, 4, f) != 4) return 0u;
        return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
               (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    };
    auto r16 = [&]() -> std::uint16_t {
        unsigned char b[2];
        if (std::fread(b, 1, 2, f) != 2) return 0u;
        return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    };

    char tag[4];
    if (std::fread(tag, 1, 4, f) != 4 || std::memcmp(tag, "RIFF", 4) != 0) { std::fclose(f); return out; }
    (void)r32();
    if (std::fread(tag, 1, 4, f) != 4 || std::memcmp(tag, "WAVE", 4) != 0) { std::fclose(f); return out; }

    std::uint16_t channels = 1, bits = 16;
    std::uint32_t fs = 44100;
    while (std::fread(tag, 1, 4, f) == 4) {
        const std::uint32_t sz = r32();
        if (std::memcmp(tag, "fmt ", 4) == 0) {
            (void)r16();             // audio format
            channels = r16();
            fs = r32();
            (void)r32();             // byte rate
            (void)r16();             // block align
            bits = r16();
            if (sz > 16) std::fseek(f, static_cast<long>(sz - 16), SEEK_CUR);
        } else if (std::memcmp(tag, "data", 4) == 0) {
            if (bits != 16 || channels == 0) break;
            const std::uint32_t nsamp = sz / 2u;
            std::vector<std::int16_t> raw(nsamp);
            if (std::fread(raw.data(), 2, nsamp, f) != nsamp) break;
            const std::uint32_t frames = nsamp / channels;
            out.resize(frames);
            for (std::uint32_t i = 0; i < frames; ++i) {
                long acc = 0;
                for (std::uint16_t c = 0; c < channels; ++c) acc += raw[i * channels + c];
                out[i] = static_cast<float>(acc) / (static_cast<float>(channels) * 32768.0f);
            }
            break;
        } else {
            std::fseek(f, static_cast<long>(sz + (sz & 1u)), SEEK_CUR);  // skip + pad byte
        }
    }
    std::fclose(f);
    fsOut = static_cast<int>(fs);
    return out;
}


inline bool write_mono16(const std::string& path, const std::vector<float>& x, int fs) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    auto u32 = [&](std::uint32_t v) {
        std::fputc(static_cast<int>(v & 0xff), f);
        std::fputc(static_cast<int>((v >> 8) & 0xff), f);
        std::fputc(static_cast<int>((v >> 16) & 0xff), f);
        std::fputc(static_cast<int>((v >> 24) & 0xff), f);
    };
    auto u16 = [&](std::uint16_t v) {
        std::fputc(static_cast<int>(v & 0xff), f);
        std::fputc(static_cast<int>((v >> 8) & 0xff), f);
    };

    const std::uint32_t n = static_cast<std::uint32_t>(x.size());
    const std::uint32_t dataBytes = n * 2u;
    std::fwrite("RIFF", 1, 4, f);
    u32(36u + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16u);                                    // PCM fmt chunk size
    u16(1u);                                     // PCM
    u16(1u);                                     // mono
    u32(static_cast<std::uint32_t>(fs));         // sample rate
    u32(static_cast<std::uint32_t>(fs) * 2u);    // byte rate
    u16(2u);                                      // block align
    u16(16u);                                     // bits/sample
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);

    for (float v : x) {
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        const std::int16_t s = static_cast<std::int16_t>(std::lrintf(v * 32767.0f));
        u16(static_cast<std::uint16_t>(s));
    }
    std::fclose(f);
    return true;
}

}  // namespace wav

#endif  // EOC_WAV_HPP
