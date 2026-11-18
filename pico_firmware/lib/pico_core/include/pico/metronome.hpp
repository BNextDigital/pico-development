#pragma once

#include <cstdint>

namespace pico {

// Beat phase is measured in ms * BPM, keeping non-integral periods exact.
// A delayed caller gets the current beat once, never a burst of stale clicks.
class Metronome {
public:
    bool set_bpm(unsigned bpm)
    {
        if (bpm < 30 || bpm > 240) return false;
        bpm_ = bpm;
        return true;
    }
    bool set_beats(unsigned beats)
    {
        if (beats < 1 || beats > 8) return false;
        beats_ = beats;
        if (beat_ > beats_) beat_ = 1;
        return true;
    }
    bool start(std::uint32_t now)
    {
        if (running_) return false;
        running_ = true; last_ms_ = now; phase_ = 0; beat_ = 1;
        return true;
    }
    void stop() { running_ = false; phase_ = 0; }
    bool advance(std::uint32_t now)
    {
        if (!running_) return false;
        const auto elapsed = now - last_ms_;
        last_ms_ = now;
        const auto phase = static_cast<std::uint64_t>(phase_) +
                           static_cast<std::uint64_t>(elapsed) * bpm_;
        const auto due = phase / 60000U;
        phase_ = static_cast<std::uint32_t>(phase % 60000U);
        if (due) beat_ = static_cast<unsigned>((beat_ - 1 + due) % beats_) + 1;
        return due != 0;
    }
    // Earliest next beat or end of its visual pulse, including fractional BPM.
    std::uint32_t next_update_ms(std::uint32_t now) const
    {
        if (!running_) return UINT32_MAX;
        const auto phase = static_cast<std::uint64_t>(phase_) +
                           static_cast<std::uint64_t>(now - last_ms_) * bpm_;
        if (phase >= 60000U) return 0;
        const auto target = phase < 80U * bpm_ ? 80U * bpm_ : 60000U;
        return static_cast<std::uint32_t>((target - phase + bpm_ - 1U) / bpm_);
    }
    // Rolling average of up to four valid tap intervals. Long gaps start over.
    bool tap(std::uint32_t now)
    {
        if (!has_tap_ || now - last_tap_ > 2000U) {
            last_tap_ = now; has_tap_ = true; tap_count_ = tap_index_ = 0;
            return false;
        }
        const auto elapsed = now - last_tap_;
        if (elapsed < 250U) return false; // Ignore bounce / accidental double taps.
        last_tap_ = now;
        taps_[tap_index_] = elapsed;
        tap_index_ = (tap_index_ + 1) % 4;
        if (tap_count_ < 4) ++tap_count_;
        unsigned sum = 0;
        for (unsigned i = 0; i < tap_count_; ++i) sum += taps_[i];
        return set_bpm((60000U * tap_count_ + sum / 2) / sum);
    }
    bool running() const { return running_; }
    unsigned bpm() const { return bpm_; }
    unsigned beats() const { return beats_; }
    unsigned beat() const { return beat_; }
    bool pulse() const { return running_ && phase_ < 80U * bpm_; }

private:
    unsigned bpm_{120}, beats_{4}, beat_{1};
    bool running_{false}, has_tap_{false};
    std::uint32_t last_ms_{0}, phase_{0}, last_tap_{0};
    unsigned taps_[4]{}, tap_count_{0}, tap_index_{0};
};

} // namespace pico
