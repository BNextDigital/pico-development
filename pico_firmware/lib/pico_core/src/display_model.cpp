#include <pico/display_model.hpp>

#include <algorithm>
#include <cstdio>
#include <pico/face_assets.hpp>

namespace pico::display {
namespace {

bool locate(const MonoSurface &surface, std::uint16_t x, std::uint16_t y,
            std::size_t &byte_index, std::uint8_t &bit)
{
    if (surface.data == nullptr || x >= surface.width || y >= surface.height) {
        return false;
    }

    switch (surface.layout) {
    case MonoLayout::HorizontalMsb: {
        const auto stride = (surface.width + 7U) / 8U;
        byte_index = static_cast<std::size_t>(y) * stride + (x / 8U);
        bit = static_cast<std::uint8_t>(7U - (x % 8U));
        break;
    }
    case MonoLayout::VerticalLsb:
        byte_index = static_cast<std::size_t>(y / 8U) * surface.width + x;
        bit = static_cast<std::uint8_t>(y % 8U);
        break;
    case MonoLayout::VerticalMsb:
        byte_index = static_cast<std::size_t>(y / 8U) * surface.width + x;
        bit = static_cast<std::uint8_t>(7U - (y % 8U));
        break;
    }

    return byte_index < surface.size;
}

void set_pixel(MonoSurface &surface, std::uint16_t x, std::uint16_t y,
               bool on = true)
{
    std::size_t byte_index = 0;
    std::uint8_t bit = 0;
    if (!locate(surface, x, y, byte_index, bit)) {
        return;
    }

    if (on) {
        surface.data[byte_index] |= static_cast<std::uint8_t>(1U << bit);
    } else {
        surface.data[byte_index] &= static_cast<std::uint8_t>(~(1U << bit));
    }
}

void fill_rect(MonoSurface &surface, std::uint16_t x, std::uint16_t y,
               std::uint16_t width, std::uint16_t height, bool on = true)
{
    const auto end_x = static_cast<std::uint32_t>(x) + width;
    const auto end_y = static_cast<std::uint32_t>(y) + height;
    for (std::uint32_t py = y; py < end_y; ++py) {
        for (std::uint32_t px = x; px < end_x; ++px) {
            set_pixel(surface, static_cast<std::uint16_t>(px),
                      static_cast<std::uint16_t>(py), on);
        }
    }
}

void outline_rect(MonoSurface &surface, std::uint16_t x, std::uint16_t y,
                  std::uint16_t width, std::uint16_t height)
{
    if (width == 0U || height == 0U) {
        return;
    }
    fill_rect(surface, x, y, width, 1);
    fill_rect(surface, x, y + height - 1U, width, 1);
    fill_rect(surface, x, y, 1, height);
    fill_rect(surface, x + width - 1U, y, 1, height);
}


void bitmap(MonoSurface &surface, const assets::Bitmap &image, int center_x, int center_y)
{
    const int left = center_x - image.width / 2;
    const int top = center_y - image.height / 2;
    const auto stride = (image.width + 7U) / 8U;
    for (unsigned y = 0; y < image.height; ++y) {
        for (unsigned x = 0; x < image.width; ++x) {
            const int px = left + static_cast<int>(x), py = top + static_cast<int>(y);
            if (px >= 0 && py >= 0 && px < surface.width && py < surface.height &&
                (image.data[y * stride + x / 8U] & (0x80U >> (x % 8U))))
                set_pixel(surface, static_cast<std::uint16_t>(px), static_cast<std::uint16_t>(py));
        }
    }
}
struct Glyph { char ch; std::uint8_t rows[5]; };
constexpr Glyph font[]{
 {'A',{2,5,7,5,5}}, {'B',{6,5,6,5,6}}, {'C',{3,4,4,4,3}}, {'D',{6,5,5,5,6}},
 {'E',{7,4,6,4,7}}, {'F',{7,4,6,4,4}}, {'G',{3,4,5,5,3}}, {'H',{5,5,7,5,5}},
 {'I',{7,2,2,2,7}}, {'J',{1,1,1,5,2}}, {'K',{5,5,6,5,5}}, {'L',{4,4,4,4,7}},
 {'M',{5,7,7,5,5}}, {'N',{5,7,7,7,5}}, {'O',{2,5,5,5,2}}, {'P',{6,5,6,4,4}},
 {'Q',{2,5,5,3,1}}, {'R',{6,5,6,5,5}}, {'S',{3,4,2,1,6}}, {'T',{7,2,2,2,2}},
 {'U',{5,5,5,5,7}}, {'V',{5,5,5,5,2}}, {'W',{5,5,7,7,5}}, {'X',{5,5,2,5,5}},
 {'Y',{5,5,2,2,2}}, {'Z',{7,1,2,4,7}}, {'0',{7,5,5,5,7}}, {'1',{2,6,2,2,7}},
 {'2',{6,1,2,4,7}}, {'3',{6,1,2,1,6}}, {'4',{5,5,7,1,1}}, {'5',{7,4,6,1,6}},
 {'6',{3,4,6,5,2}}, {'7',{7,1,2,2,2}}, {'8',{2,5,2,5,2}}, {'9',{2,5,3,1,6}},
 {'#',{5,7,5,7,5}}, {'+',{0,2,7,2,0}}, {':',{0,2,0,2,0}}, {'-',{0,0,7,0,0}}, {'>',{4,2,1,2,4}}, {'/',{1,1,2,4,4}}
};
void text(MonoSurface &surface, const char *str, unsigned x, unsigned y, unsigned scale = 1)
{
    for (; *str; ++str, x += 4U * scale) {
        for (const auto &glyph : font) {
            if (glyph.ch != *str) continue;
            for (unsigned row = 0; row < 5; ++row)
                for (unsigned col = 0; col < 3; ++col)
                    if (glyph.rows[row] & (4U >> col))
                        fill_rect(surface, x + col * scale, y + row * scale, scale, scale);
        }
    }
}
void duration(char *out, std::size_t size, std::uint32_t seconds)
{
    if (seconds >= 3600)
        std::snprintf(out, size, "%u:%02u:%02u", static_cast<unsigned>(seconds / 3600),
                      static_cast<unsigned>(seconds / 60 % 60), static_cast<unsigned>(seconds % 60));
    else
        std::snprintf(out, size, "%02u:%02u", static_cast<unsigned>(seconds / 60),
                      static_cast<unsigned>(seconds % 60));
}
void render_face(const State &state, MonoSurface &surface)
{
    const auto ti = static_cast<unsigned>(state.theme);
    const auto mi = static_cast<unsigned>(state.mood);
    const auto &face = assets::faces[ti < 8 ? ti : 0][mi < 5 ? mi : 0];
    const int cx = surface.width / 2 + state.gaze_x * 2;
    // Original face anchors are on a 128x68 canvas. Center-crop to the target;
    // blitting clips signed coordinates and never stretches source pixels.
    const int crop = (static_cast<int>(surface.height) - 68) / 2;
    bitmap(surface, face.brow_left, cx - 22, 13 + crop);
    bitmap(surface, face.brow_right, cx + 22, 13 + crop);
    if (state.eyes_closed || state.mood == Mood::Sleepy) {
        fill_rect(surface, static_cast<std::uint16_t>(cx - 22 - face.left.width / 2),
                  surface.height / 2 - 6, face.left.width, 2);
        fill_rect(surface, static_cast<std::uint16_t>(cx + 22 - face.right.width / 2),
                  surface.height / 2 - 6, face.right.width, 2);
    } else {
        bitmap(surface, face.left, cx - 22, 28 + crop);
        bitmap(surface, face.right, cx + 22, 28 + crop);
    }
    bitmap(surface, face.mouth, cx, 47 + crop);
    if (state.mood == Mood::Alert) {
        fill_rect(surface, surface.width - 15, 25, 2, 8);
        fill_rect(surface, surface.width - 15, 36, 2, 2);
    }
}
void render_status(const State &state, MonoSurface &surface)
{
    if (state.connected) fill_rect(surface, 3, 3, 3, 3);
    else outline_rect(surface, 3, 3, 3, 3);
    if (state.dock != Dock::None) outline_rect(surface, surface.width - 10, 3, 7, 6);
    if (state.focus_waiting) text(surface, state.focus_phase == FocusPhase::Work ? "BREAK READY - TAP" : "WORK READY - TAP", 20, surface.height - 7);
    if (state.timer_running || state.timer_paused) {
        char buffer[16]; duration(buffer, sizeof(buffer), state.timer_remaining_seconds);
        text(surface, buffer, 8, surface.height - 7);
        text(surface, state.timer_paused ? "PAUSED" : state.focus_phase == FocusPhase::Break ? "BREAK" : state.mode == Mode::Focus ? "FOCUS" : "TIMER", 72, surface.height - 7);
    }
}
void render_screen(const State &state, MonoSurface &surface)
{
    if (state.timer_completed) {
        render_face(state, surface);
        text(surface, state.focus_phase == FocusPhase::Work ? "WORK DONE" :
                      state.focus_phase == FocusPhase::Break ? "BREAK DONE" : "DONE", 44, 1);
        return;
    }
    if (state.screen == Screen::Home || state.screen == Screen::Themes) {
        render_face(state, surface);
        if (state.screen == Screen::Home) render_status(state, surface);
        else {
            text(surface, "THEME", 2, 1);
            text(surface, assets::theme_names[static_cast<unsigned>(state.theme) % 8], 22, surface.height - 7);
        }
        return;
    }
    if (state.screen == Screen::Tuner) {
        text(surface,"TUNER",2,2);
        text(surface,state.tuner_running ? "MIC ON" : "MIC OFF",96,2);
        outline_rect(surface,2,10,124,6);
        fill_rect(surface,4,12,state.tuner.level * 120U / 100U,2);
        if (!state.microphone_ready) {
            text(surface,"MIC UNAVAILABLE",4,25,2);
            text(surface,"CHECK BUILD / LOGS",20,43);
        } else if (!state.tuner_running) {
            text(surface,"STOPPED",28,25,2);
            text(surface,"D0 TO LISTEN",24,43);
        } else if (state.tuner.signal == pitch::Signal::Detected) {
            char note[12], cents[20];
            std::snprintf(note,sizeof(note),"%s%d",pitch::note_name(state.tuner.note),static_cast<int>(state.tuner.note)/12-1);
            text(surface,note,42,21,3);
            const int offset = state.tuner.cents;
            text(surface,offset < -5 ? "FLAT" : offset > 5 ? "SHARP" : "IN TUNE",44,40);
            fill_rect(surface,14,53,102,1);
            outline_rect(surface,59,49,11,9);
            fill_rect(surface,static_cast<unsigned>(64+offset),50,2,8);
            std::snprintf(cents,sizeof(cents),"%+d CENTS",offset);
            text(surface,cents,2,59);
            text(surface,"D0 STOP",96,59);
            return;
        } else {
            const char *message = state.tuner.signal == pitch::Signal::Quiet ? "TOO QUIET" :
                state.tuner.signal == pitch::Signal::Clipping ? "TOO LOUD" :
                state.tuner.signal == pitch::Signal::Noisy ? "HOLD ONE NOTE" : "PLUCK ONE NOTE";
            text(surface,message,12,28,2);
        }
        text(surface,"HOLD TO GO BACK",28,59);
        return;
    }
    if (state.screen == Screen::Music) {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%u BPM", static_cast<unsigned>(state.metronome_bpm));
        text(surface, buffer, 8, 2, 2);
        text(surface, state.sound_muted ? "MUTE" : "MUSIC", 104, 4);
        for (unsigned i = 0; i < state.beats_per_bar; ++i) {
            const unsigned x = 4 + i * 15;
            if (state.metronome_running && state.beat_pulse && state.current_beat == i + 1)
                fill_rect(surface, x, 18, 11, 12);
            else outline_rect(surface, x, 18, 11, 12);
        }
        std::snprintf(buffer, sizeof(buffer), "BEAT %u/%u", static_cast<unsigned>(state.current_beat),
                      static_cast<unsigned>(state.beats_per_bar));
        text(surface, buffer, 4, 32);
        text(surface, state.metronome_running ? "RUN" : "STOPPED", 88, 32);
        const char *labels[]{"BPM-", "BPM+", state.metronome_running ? "STOP" : "START",
                             "TAP", "BAR", "TUNER", "HOME"};
        for (unsigned i = 0; i < 7; ++i) {
            const auto x = 1 + (i % 4) * 32, y = 41 + (i / 4) * 12;
            if (i == state.music_index) outline_rect(surface, x, y, 30, 11);
            text(surface, labels[i], x + 3, y + 3);
        }
        return;
    }
    if (state.screen == Screen::Menu) {
        constexpr const char *items[]{"FOCUS TIMER","COOKING TIMER","HOME","MUSIC",
                                      "WAYFINDING","NOTES","FACE THEMES","BACK"};
        text(surface, "PICO MENU", 2, 2, 2);
        const auto first = (state.menu_index / 4) * 4;
        for (unsigned i = 0; i < 4; ++i) {
            const auto index = first + i;
            text(surface, index == state.menu_index ? ">" : " ", 2, 20 + i * 10);
            text(surface, items[index], 10, 20 + i * 10);
        }
        return;
    }
    if (state.screen == Screen::Unavailable) {
        text(surface, "NOT READY", 8, 10, 2);
        text(surface, "FEATURE NOT IMPLEMENTED", 4, 32);
        text(surface, "SELECT TO GO BACK", 4, 48);
        return;
    }
    char buffer[24];
    duration(buffer, sizeof(buffer), state.timer_running || state.timer_paused ?
             state.timer_remaining_seconds : state.configured_duration_seconds);
    text(surface, buffer, 24, 1, 2);
    text(surface, state.focus_phase == FocusPhase::Break ? "BREAK TIMER" :
                  state.mode == Mode::Cooking ? "COOKING TIMER" : "FOCUS TIMER", 20, 15);
    constexpr const char *actions[]{"DURATION", "START", "CANCEL", "HOME", "WORK/BREAK"};
    for (unsigned i = 0; i < (state.mode == Mode::Focus ? 5U : 4U); ++i) {
        text(surface, state.utility_index == i ? ">" : " ", 2, 23 + i * 8);
        const char *label = i == 1 && state.timer_running ? "PAUSE" :
                            i == 1 && state.timer_paused ? "RESUME" :
                            i == 4 && state.focus_waiting ? "NEXT PHASE" :
                            i == 4 && state.focus_phase != FocusPhase::None ? "CYCLE ACTIVE" : actions[i];
        text(surface, label, 10, 23 + i * 8);
        if (i == 0) {
            duration(buffer, sizeof(buffer), state.configured_duration_seconds);
            text(surface, buffer, 64, 23);
        }
    }
}

} // namespace

std::size_t required_buffer_size(const MonoSurface &surface)
{
    if (surface.layout == MonoLayout::HorizontalMsb) {
        return static_cast<std::size_t>((surface.width + 7U) / 8U) *
               surface.height;
    }
    return static_cast<std::size_t>(surface.width) *
           ((surface.height + 7U) / 8U);
}

bool render(const State &state, MonoSurface surface)
{
    if (surface.data == nullptr || surface.width < 64U || surface.height < 32U ||
        surface.size < required_buffer_size(surface)) {
        return false;
    }

    std::fill(surface.data, surface.data + required_buffer_size(surface), 0U);
    render_screen(state, surface);
    return true;
}

bool pixel_at(const MonoSurface &surface, std::uint16_t x, std::uint16_t y)
{
    std::size_t byte_index = 0;
    std::uint8_t bit = 0;
    return locate(surface, x, y, byte_index, bit) &&
           (surface.data[byte_index] & static_cast<std::uint8_t>(1U << bit)) != 0U;
}

} // namespace pico::display
