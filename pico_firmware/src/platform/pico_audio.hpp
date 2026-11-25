#pragma once
#include <pico/audio.hpp>
namespace pico::platform {
class Audio {
public:
    bool init();
    bool ready() const;
    void submit_post_beep(bool muted, unsigned volume_percent);
    void set_metronome_running(bool running);
    void set_muted(bool muted);
    void set_owner(audio::Owner owner);
    void set_reference(bool running, std::uint8_t note);
    void play(audio::Cue cue,unsigned volume_percent = 25, std::uint8_t note = 69);
private:
#if defined(CONFIG_PICO_AUDIO_POST_BEEP)
    bool post_checked_{false};
#endif
};
}
