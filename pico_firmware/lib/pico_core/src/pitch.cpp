#include <pico/pitch.hpp>
#include <algorithm>

namespace pico::pitch {
Reading Detector::analyze(const std::int16_t *samples, std::size_t count)
{
    Reading out;
    if (!samples || count != kFrameSamples) { reset(); out.signal = Signal::Noisy; return out; }
    std::int64_t total = 0;
    unsigned clipped = 0;
    for (std::size_t i=0; i<count; ++i) {
        total += samples[i];
        if (samples[i] >= 32700 || samples[i] <= -32700) ++clipped;
    }
    const auto mean = static_cast<std::int32_t>(total / static_cast<std::int64_t>(count));
    std::uint64_t energy = 0, absolute = 0;
    for (std::size_t i=0; i<count; ++i) {
        const auto centered = static_cast<std::int32_t>(samples[i]) - mean;
        energy += static_cast<std::uint64_t>(static_cast<std::int64_t>(centered) * centered);
        absolute += centered < 0 ? -centered : centered;
    }
    out.level = static_cast<std::uint8_t>(std::min<std::uint64_t>(100, absolute / count / 100));
    if (clipped > count / 100) { reset(); out.signal = Signal::Clipping; return out; }
    if (energy / count < 80U * 80U) { reset(); out.signal = Signal::Quiet; return out; }

    // YIN cumulative mean normalized difference. Constant-size comparisons
    // remove DC and avoid window-length bias. Workspaces are caller-owned.
    float running = 0;
    normalized_[0] = 1.0f;
    for (unsigned lag=1; lag<=kMaxLag; ++lag) {
        std::uint64_t difference = 0;
        for (unsigned i=0; i<1024; ++i) {
            const auto delta = static_cast<std::int32_t>(samples[i]) - samples[i+lag];
            difference += static_cast<std::uint64_t>(static_cast<std::int64_t>(delta) * delta);
        }
        const auto value = static_cast<float>(difference);
        running += value;
        normalized_[lag] = running > 0 ? value * static_cast<float>(lag) / running : 1.0f;
    }
    unsigned lag = 2; // Inspect short periods too, rejecting out-of-range fundamentals below.
    while (lag < kMaxLag && normalized_[lag] >= 0.15f) ++lag;
    if (lag == kMaxLag) { reset(); out.signal = Signal::Noisy; return out; }
    while (lag+1 < kMaxLag && normalized_[lag+1] < normalized_[lag]) ++lag;
    // Interpolate the raw difference minimum: normalizing the three points
    // independently biases high-note estimates when the period is short.
    const auto raw_difference = [&](unsigned shift) {
        std::uint64_t sum = 0;
        for (unsigned i=0; i<1024; ++i) {
            const auto delta = static_cast<std::int32_t>(samples[i]) - samples[i+shift];
            sum += static_cast<std::uint64_t>(static_cast<std::int64_t>(delta) * delta);
        }
        return static_cast<float>(sum);
    };
    const auto left = raw_difference(lag-1), center = raw_difference(lag), right = raw_difference(lag+1);
    const auto denominator = left - 2.0f * center + right;
    const auto adjustment = denominator > 0 ? std::clamp(0.5f * (left-right) / denominator,-1.0f,1.0f) : 0.0f;
    const auto hz = static_cast<float>(kSampleRate) / (static_cast<float>(lag) + adjustment);
    if (hz < 35.0f || hz > 1400.0f) { reset(); out.signal = Signal::Noisy; return out; }
    int midi = 69;
    float reference = 440.0f;
    while (hz < reference * 0.97153194f) { reference /= 1.059463094f; --midi; }
    while (hz > reference * 1.029302237f) { reference *= 1.059463094f; ++midi; }
    // ln(hz/reference), using the rapidly convergent atanh series near unity.
    const float x = (hz-reference)/(hz+reference), square = x*x;
    const float cents = 3462.468098f * x * (1.0f + square/3.0f + square*square/5.0f);
    out.note = static_cast<std::uint8_t>(midi);
    out.cents = static_cast<std::int8_t>(std::clamp(static_cast<int>(cents + (cents >= 0 ? 0.5f : -0.5f)),-50,50));
    if (last_note_ != out.note) { last_note_ = out.note; hits_ = 0; recent_index_ = 0; }
    recent_[recent_index_] = out.cents;
    recent_index_ = static_cast<std::uint8_t>((recent_index_+1) % 3);
    if (hits_ < 3) ++hits_;
    if (hits_ < 2) { out.signal = Signal::Listening; return out; }
    if (hits_ >= 3) {
        auto sorted = recent_;
        std::sort(sorted.begin(),sorted.end());
        out.cents = sorted[1];
    }
    out.signal = Signal::Detected;
    return out;
}
std::uint32_t pack(const Reading &r, std::uint8_t session)
{
    return static_cast<std::uint32_t>(r.note) | (static_cast<std::uint32_t>(r.cents+50) << 7U) |
        (static_cast<std::uint32_t>(r.level) << 14U) | (static_cast<std::uint32_t>(r.signal) << 21U) |
        (static_cast<std::uint32_t>(session) << 24U);
}
bool unpack(std::uint32_t value, Reading &r, std::uint8_t &session)
{
    const auto cents = (value >> 7U) & 127U, level = (value >> 14U) & 127U, signal = (value >> 21U) & 7U;
    if (cents > 100U || level > 100U || signal > static_cast<unsigned>(Signal::Unavailable)) return false;
    r = {static_cast<Signal>(signal), static_cast<std::uint8_t>(value & 127U),
         static_cast<std::int8_t>(static_cast<int>(cents)-50), static_cast<std::uint8_t>(level)};
    session = static_cast<std::uint8_t>(value >> 24U);
    return true;
}
const char *note_name(std::uint8_t midi)
{
    constexpr const char *names[]{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return names[midi % 12];
}
} // namespace pico::pitch
