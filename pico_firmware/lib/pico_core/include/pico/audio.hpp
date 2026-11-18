#pragma once
#include <cstddef>
#include <cstdint>
namespace pico::audio {
enum class Cue : std::uint8_t { Interact, TimerComplete, WorkComplete, BreakComplete, MetronomeClick, MetronomeAccent, Count };
inline constexpr std::uint32_t kSampleRate = 31250;
inline constexpr std::int16_t kPeakLimit = 2048;
std::uint32_t frames(Cue cue);
// Identical left/right 16-bit PCM. Volume is clamped to 0..100; peak <= 2048.
void render(Cue cue, std::uint32_t first_frame, std::int16_t *stereo,
            std::size_t frame_count, unsigned volume_percent = 25);
}
