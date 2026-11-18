// Production display adapter with a fake driver: no target/electrical claims.
#include "platform/pico_display.hpp"
#include <zephyr/drivers/display.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <vector>
namespace {
const device panel{};
int on_error, off_error, contrast_error, write_error;
unsigned writes, contrasts, blanks, unblanks, failures;
std::uint8_t last_contrast;
std::vector<char> calls;
std::array<std::uint8_t,1024> sent{};
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while (false)
[[maybe_unused]] void reset()
{ on_error=off_error=contrast_error=write_error=0; writes=contrasts=blanks=unblanks=0; calls.clear(); }
}
const device *fake_display_device() { return &panel; }
bool device_is_ready(const device *) { return true; }
void display_get_capabilities(const device *, display_capabilities *caps)
{ *caps={128,64,SCREEN_INFO_MONO_VTILED}; }
int display_set_pixel_format(const device *, unsigned) { return 0; }
int display_blanking_on(const device *) { ++blanks; calls.push_back('B'); return on_error; }
int display_blanking_off(const device *) { ++unblanks; calls.push_back('O'); return off_error; }
int display_set_contrast(const device *, std::uint8_t contrast)
{ ++contrasts; last_contrast=contrast; calls.push_back('C'); return contrast_error; }
int display_write(const device *, unsigned, unsigned, const display_buffer_descriptor *desc, const void *data)
{
    ++writes; calls.push_back('W'); CHECK(desc->buf_size == sent.size());
    std::copy_n(static_cast<const std::uint8_t *>(data),sent.size(),sent.begin());
    return write_error;
}
int main()
{
    pico::platform::Display display;
    pico::State state;
#if defined(CONFIG_PICO_DISPLAY)
    reset(); CHECK(display.init()); CHECK(display.render(state));
    CHECK(writes == 1 && contrasts == 1 && last_contrast == 128);
    CHECK(display.render(state)); CHECK(writes == 1 && contrasts == 1);
    state.screen=pico::Screen::SettingEdit; state.setting_key=pico::SettingKey::Brightness;
    state.setting_draft=5; CHECK(display.render(state)); CHECK(last_contrast == 13);
    state.screen=pico::Screen::Home; CHECK(display.render(state)); CHECK(last_contrast == 128);
    state.display_asleep=true; CHECK(display.render(state));
    const auto before=writes;
    CHECK(blanks == 1); CHECK(display.render(state)); CHECK(writes == before && blanks == 1);
    state.display_asleep=false; calls.clear(); CHECK(display.render(state));
    CHECK(calls == std::vector<char>({'W','O'})); // full frame restored before wake
    CHECK(writes == before+1);
    state.display_asleep=true; CHECK(display.render(state));
    off_error=-EIO; state.display_asleep=false; CHECK(!display.render(state));
    off_error=0; calls.clear(); CHECK(display.render(state));
    CHECK(calls == std::vector<char>({'W','O'}));

    reset(); CHECK(display.init()); contrast_error=-ENOSYS; on_error=-ENOSYS;
    CHECK(display.render(state)); CHECK(contrasts == 1);
    state.display_brightness_percent=75; CHECK(display.render(state)); CHECK(contrasts == 1);
    state.display_asleep=true; CHECK(display.render(state));
    CHECK(std::all_of(sent.begin(),sent.end(),[](auto value){ return value==0; }));
    const auto fallback_writes=writes;
    CHECK(display.render(state)); CHECK(blanks == 1 && writes == fallback_writes);
    state.display_asleep=false; CHECK(display.render(state));
    CHECK(std::any_of(sent.begin(),sent.end(),[](auto value){ return value!=0; }));
    write_error=-EIO; state.mood=pico::Mood::Happy; CHECK(!display.render(state));
    write_error=0; CHECK(display.render(state));
#else
    CHECK(!display.init()); CHECK(!display.render(state));
    CHECK(display.capabilities() == pico::capability(pico::Capability::None));
#endif
    if (failures) return EXIT_FAILURE;
    std::cout << "OLED adapter model tests passed\n";
}
