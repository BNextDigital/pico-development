#pragma once
#include <cstddef>
#include <cstdint>
namespace pico::audio {
// Single worker ownership; changing owner invalidates queued/in-flight stale cues.
enum class Owner : std::uint8_t { Feature, LocalFind, Timer, Alarm, NetworkRing, UnwantedTracking };
enum class Cue : std::uint8_t { Interact, TimerComplete, WorkComplete, BreakComplete, MetronomeClick, MetronomeAccent, ReferenceTone, Count };
// nRF52840 PCLK32M_HFXO, MCK /21, MCK/LRCLK 32, 16-bit stereo.
// Request the integer rate selected by nrfx so synthesis and DMA agree.
inline constexpr std::uint32_t kClockSourceHz = 32000000;
inline constexpr unsigned kMckDivider = 21;
inline constexpr unsigned kMckRatio = 32;
inline constexpr unsigned kWordBits = 16;
inline constexpr unsigned kChannels = 2;
inline constexpr std::uint32_t kSampleRate = kClockSourceHz / (kMckDivider * kMckRatio);
inline constexpr std::uint32_t kBitClockHz =
    kClockSourceHz * (kWordBits * kChannels) / (kMckDivider * kMckRatio);
inline constexpr std::uint32_t kReferenceDurationMs = 2000;
inline constexpr std::uint32_t kStartupSilenceMs = 8;
// UI timeout includes clock startup, leading silence and the final DMA stop.
inline constexpr std::uint32_t kReferencePlaybackMs = kReferenceDurationMs + kStartupSilenceMs + 12;
constexpr std::uint32_t frames_for_ms(unsigned ms)
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(ms) * kSampleRate / 1000U);
}
inline constexpr std::int16_t kPeakLimit = 2048;
inline constexpr std::uint32_t kReferenceFrames = frames_for_ms(kReferenceDurationMs);
std::uint32_t frequency_millihz(std::uint8_t midi_note);
void render_reference(std::uint8_t midi_note, std::uint32_t first_frame,
                      std::int16_t *stereo, std::size_t frame_count, unsigned volume_percent = 25);
std::uint32_t frames(Cue cue);
// Identical left/right 16-bit PCM. Volume is clamped to 0..100; peak <= 2048.
void render(Cue cue, std::uint32_t first_frame, std::int16_t *stereo,
            std::size_t frame_count, unsigned volume_percent = 25);
}
