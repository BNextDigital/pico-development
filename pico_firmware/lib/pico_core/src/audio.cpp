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
    case Cue::ReferenceTone: case Cue::Count: break;
    }
    return {nullptr,0};
}
// Integer triangle oscillator with a short attack/release envelope. No heap,
// floating-point trigonometry, platform clock or state shared across cues.
std::int16_t sample(Cue cue, std::uint32_t frame, unsigned volume)
{
    const auto s = score(cue);
    for (std::size_t i=0; i<s.count; ++i) {
        const auto count = frames_for_ms(s.notes[i].ms);
        if (frame >= count) { frame -= count; continue; }
        if (!s.notes[i].frequency) return 0;
        const auto phase = static_cast<unsigned>((static_cast<std::uint64_t>(frame) *
            s.notes[i].frequency * 65536U / kSampleRate) & 65535U);
        const int triangle = phase < 32768U ? static_cast<int>(phase) * 2 - 32768 :
                                             98303 - static_cast<int>(phase) * 2;
        const auto fade = std::min({frame, count-1-frame, frames_for_ms(5)});
        const auto scaled = static_cast<std::int64_t>(triangle) * kPeakLimit *
                            std::min(volume,100U) * fade;
        return static_cast<std::int16_t>(scaled / (32768LL * 100 * (frames_for_ms(5))));
    }
    return 0;
}
}
std::uint32_t frames(Cue cue)
{
    if (cue == Cue::ReferenceTone) return kReferenceFrames;
    const auto s = score(cue);
    std::uint32_t total=0;
    for (std::size_t i=0; i<s.count; ++i) total += frames_for_ms(s.notes[i].ms);
    return total;
}
void render(Cue cue, std::uint32_t first, std::int16_t *out, std::size_t count, unsigned volume)
{
    if (cue == Cue::ReferenceTone) { render_reference(69,first,out,count,volume); return; }
    if (!out) return;
    const auto length = frames(cue);
    for (std::size_t i=0; i<count; ++i) {
        const auto pos = static_cast<std::uint64_t>(first) + i;
        const auto value = pos < length ? sample(cue,static_cast<std::uint32_t>(pos),volume) : 0;
        out[2*i] = out[2*i+1] = static_cast<std::int16_t>(value);
    }
}
std::uint32_t frequency_millihz(std::uint8_t note)
{
    // Equal temperament, A4=440 Hz, E2..C6. No runtime floating point.
    constexpr std::uint32_t pitches[]{82407U,87307U,92499U,97999U,103826U,110000U,116541U,123471U,130813U,138591U,146832U,155563U,164814U,174614U,184997U,195998U,207652U,220000U,233082U,246942U,261626U,277183U,293665U,311127U,329628U,349228U,369994U,391995U,415305U,440000U,466164U,493883U,523251U,554365U,587330U,622254U,659255U,698456U,739989U,783991U,830609U,880000U,932328U,987767U,1046502U};
    return note >= 40 && note <= 84 ? pitches[note-40] : 0;
}
void render_reference(std::uint8_t note, std::uint32_t first, std::int16_t *out,
                      std::size_t count, unsigned volume)
{
    if (!out) return;
    const auto hz = frequency_millihz(note);
    for (std::size_t i=0; i<count; ++i) {
        const auto frame = static_cast<std::uint64_t>(first) + i;
        std::int16_t value = 0;
        if (hz && frame < kReferenceFrames) {
            const auto phase = static_cast<unsigned>((frame * hz * 65536U / (kSampleRate * 1000ULL)) & 65535U);
            const int triangle = phase < 32768U ? static_cast<int>(phase)*2-32768 : 98303-static_cast<int>(phase)*2;
            const auto fade = std::min({frame, kReferenceFrames-1-frame, static_cast<std::uint64_t>(frames_for_ms(5))});
            value = static_cast<std::int16_t>(static_cast<std::int64_t>(triangle)*kPeakLimit*
                std::min(volume,100U)*static_cast<std::int64_t>(fade)/(32768LL*100*(frames_for_ms(5))));
        }
        out[2*i] = out[2*i+1] = value;
    }
}

}
