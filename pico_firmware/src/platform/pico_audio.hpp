#pragma once
#include <pico/audio.hpp>
namespace pico::platform {
class Audio {
public:
    bool init();
    bool ready() const;
    void set_metronome_running(bool running);
    void set_muted(bool muted);
    void play(audio::Cue cue,unsigned volume_percent = 25);
};
}
