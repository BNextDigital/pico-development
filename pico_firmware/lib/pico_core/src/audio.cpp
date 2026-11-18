#include <pico/audio.hpp>
#include <algorithm>
namespace pico::audio {
namespace {
struct Note { unsigned frequency, ms; };
constexpr Note interaction[]{{880,60}};
constexpr Note timer[]{{660,70},{0,45},{880,100}};
constexpr Note work[]{{660,65},{0,40},{880,65},{0,40},{1100,90}};
constexpr Note click[]{{1600,12}};
constexpr Note accent[]{{2400,16}};
constexpr Note rest[]{{880,80},{0,50},{660,100}};
struct Score { const Note *notes; std::size_t count; };
Score score(Cue cue)
{
    switch (cue) {
    case Cue::Interact: return {interaction,1};
    case Cue::TimerComplete: return {timer,3};
    case Cue::WorkComplete: return {work,5};
    case Cue::BreakComplete: return {rest,3};
    case Cue::MetronomeClick: return {click,1};
    case Cue::MetronomeAccent: return {accent,1};
    case Cue::Count: break;
    }
    return {nullptr,0};
}
// Integer triangle oscillator with a short attack/release envelope. No heap,
// floating-point trigonometry, platform clock or state shared across cues.
std::int16_t sample(Cue cue, std::uint32_t frame, unsigned volume)
{
    const auto s = score(cue);
    for (std::size_t i=0; i<s.count; ++i) {
        const auto count = s.notes[i].ms * kSampleRate / 1000U;
        if (frame >= count) { frame -= count; continue; }
        if (!s.notes[i].frequency) return 0;
        const auto phase = static_cast<unsigned>((static_cast<std::uint64_t>(frame) *
            s.notes[i].frequency * 65536U / kSampleRate) & 65535U);
        const int triangle = phase < 32768U ? static_cast<int>(phase) * 2 - 32768 :
                                             98303 - static_cast<int>(phase) * 2;
        const auto fade = std::min({frame, count-1-frame, kSampleRate / 200U});
        const auto scaled = static_cast<std::int64_t>(triangle) * kPeakLimit *
                            std::min(volume,100U) * fade;
        return static_cast<std::int16_t>(scaled / (32768LL * 100 * (kSampleRate / 200U)));
    }
    return 0;
}
}
std::uint32_t frames(Cue cue)
{
    const auto s = score(cue);
    std::uint32_t total=0;
    for (std::size_t i=0; i<s.count; ++i) total += s.notes[i].ms * kSampleRate / 1000U;
    return total;
}
void render(Cue cue, std::uint32_t first, std::int16_t *out, std::size_t count, unsigned volume)
{
    if (!out) return;
    const auto length = frames(cue);
    for (std::size_t i=0; i<count; ++i) {
        const auto pos = static_cast<std::uint64_t>(first) + i;
        const auto value = pos < length ? sample(cue,static_cast<std::uint32_t>(pos),volume) : 0;
        out[2*i] = out[2*i+1] = static_cast<std::int16_t>(value);
    }
}
}
