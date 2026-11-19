#pragma once
#include <cstddef>
#include <cstdint>
namespace pico::audio {
// Single worker ownership; changing owner invalidates queued/in-flight stale cues.
enum class Owner : std::uint8_t { Feature, LocalFind, Timer, Alarm, NetworkRing, UnwantedTracking };
enum class Cue : std::uint8_t { Interact, TimerComplete, WorkComplete, BreakComplete, MetronomeClick, MetronomeAccent, ReferenceTone, Count };
inline constexpr std::uint32_t kSampleRate = 31250;
inline constexpr std::int16_t kPeakLimit = 2048;
inline constexpr std::uint32_t kReferenceFrames = kSampleRate * 2U;
std::uint32_t frequency_millihz(std::uint8_t midi_note);
void render_reference(std::uint8_t midi_note, std::uint32_t first_frame,
                      std::int16_t *stereo, std::size_t frame_count, unsigned volume_percent = 25);
std::uint32_t frames(Cue cue);
// Identical left/right 16-bit PCM. Volume is clamped to 0..100; peak <= 2048.
void render(Cue cue, std::uint32_t first_frame, std::int16_t *stereo,
            std::size_t frame_count, unsigned volume_percent = 25);
}
