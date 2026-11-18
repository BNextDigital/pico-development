#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace pico::pitch {
inline constexpr unsigned kSampleRate = 15625; // 1 MHz PDM clock / 64, exact.
inline constexpr std::size_t kFrameSamples = 2048;
enum class Signal : std::uint8_t { Listening, Quiet, Noisy, Clipping, Detected, Unavailable };
struct Reading {
    Signal signal{Signal::Listening};
    std::uint8_t note{69}; // MIDI convention: A4 = 69, C4 = 60.
    std::int8_t cents{0};
    std::uint8_t level{0};
};
class Detector {
public:
    Reading analyze(const std::int16_t *samples, std::size_t count);
    void reset() { last_note_ = 255; hits_ = 0; recent_index_ = 0; }
private:
    static constexpr unsigned kMaxLag = 448;
    std::array<float,kMaxLag+1> normalized_{};
    std::uint8_t last_note_{255}, hits_{0}, recent_index_{0};
    std::array<std::int8_t,3> recent_{};
};
std::uint32_t pack(const Reading &reading, std::uint8_t session);
bool unpack(std::uint32_t value, Reading &reading, std::uint8_t &session);
const char *note_name(std::uint8_t midi);
} // namespace pico::pitch
