#pragma once
#include <pico/audio.hpp>
namespace pico::platform {
class Audio {
public:
    bool init();
    bool ready() const;
    void set_metronome_running(bool running);
    void set_muted(bool muted);
    void set_owner(audio::Owner owner);
    void set_reference(bool running, std::uint8_t note);
    void play(audio::Cue cue,unsigned volume_percent = 25, std::uint8_t note = 69);
};
}
