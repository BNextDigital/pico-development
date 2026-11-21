#include <pico/display_model.hpp>

#include <algorithm>
#include <cstdio>
#include <pico/face_assets.hpp>
#include <pico/menu_icons.hpp>
#include <cstring>

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
 {'.',{0,0,0,0,2}}, {'=',{0,7,0,7,0}},
 {'<',{1,2,4,2,1}}, {'%',{5,1,2,4,5}},
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
// Pocket layout: one selected action, a large value and a short context line.
void centered(MonoSurface &surface, const char *label, unsigned y, unsigned preferred)
{
    unsigned scale=preferred;
    const unsigned available=surface.width-12U;
    while (scale>1 && std::strlen(label)*4U*scale-scale>available) --scale;
    char clipped[32]{};
    const auto count=std::min<std::size_t>(sizeof(clipped)-1,(available+scale)/(4U*scale));
    std::strncpy(clipped,label,count);
    const auto width=std::strlen(clipped)*4U*scale-(clipped[0] ? scale : 0U);
    text(surface,clipped,(surface.width-width)/2U,y,scale);
}
void selected_action(MonoSurface &surface, const char *label, unsigned index, unsigned count)
{
    const bool large=surface.height>=56 && surface.width>=96;
    const auto y=large ? 52U : 26U;
    centered(surface,label,y,large ? 2U : 1U);
    // Navigation hint, not a grid of buttons competing with the main value.
    text(surface,"<",2,y,1); text(surface,">",surface.width-5,y,1);
    if (large && count) {
        const unsigned total=count*5U;
        const unsigned left=(surface.width-total)/2U;
        for (unsigned i=0; i<count; ++i) fill_rect(surface,left+i*5U,63,i==index ? 3 : 1,1);
    }
}
void card(MonoSurface &surface, const char *title, const char *value,
          const char *context, const char *action, unsigned index=0, unsigned count=0)
{
    const bool large=surface.height>=56 && surface.width>=96;
    centered(surface,title,0,large ? 2U : 1U);
    centered(surface,value,large ? 15U : 8U,large ? 4U : 2U);
    centered(surface,context,large ? 37U : 20U,large ? 2U : 1U);
    selected_action(surface,action,index,count);
}
void render_status(const State &state, MonoSurface &surface)
{
    // Keep the companion face clean. BLE/motion/dock flags remain in diagnostics.
    char value[24]{};
    if (state.focus_waiting) std::snprintf(value,sizeof(value),"READY");
    else if (state.timer_running || state.timer_paused)
        duration(value,sizeof(value),state.timer_remaining_seconds);
    if (!value[0] && state.clock.settings.glance) {
        if (state.clock.valid) {
            const auto date=clock::from_epoch(static_cast<std::int64_t>(state.clock.utc)+state.clock.settings.offset_minutes*60);
            clock::format_time(value,sizeof(value),date,state.clock.settings.hour24);
            if (!state.clock.settings.hour24) {
                const auto length=std::strlen(value);
                std::snprintf(value+length,sizeof(value)-length," %s",date.hour<12 ? "AM" : "PM");
            }
        } else std::snprintf(value,sizeof(value),"--:--");
    }
    if (value[0]) centered(surface,value,surface.height-11U,2);
}

void clock_card(const State &s, MonoSurface &surface, bool desk=false)
{
    char value[16]="--:--",date[24]="SET TIME";
    const auto &clock=s.clock;
    if (clock.valid) {
        const auto d=clock::from_epoch(static_cast<std::int64_t>(clock.utc)+clock.settings.offset_minutes*60);
        clock::format_time(value,sizeof(value),d,clock.settings.hour24);
        clock::format_date(date,sizeof(date),d);
        if (!clock.settings.hour24) {
            const auto length=std::strlen(date);
            std::snprintf(date+length,sizeof(date)-length," %s",d.hour<12 ? "AM" : "PM");
        }
    }
    constexpr const char *actions[]{"ALARMS","STOPWATCH","SET TIME","OPTIONS","HOME"};
    card(surface,desk ? "PICO DESK" : "CLOCK",value,date,desk ? "MENU" : actions[s.clock_index%5],s.clock_index,desk ? 0 : 5);
}
void render_clock_utility(const State &s, MonoSurface &surface)
{
    if (s.screen==Screen::Clock) { clock_card(s,surface); return; }
    if (s.screen==Screen::Stopwatch) {
        char elapsed[24]; duration(elapsed,sizeof(elapsed),s.clock.stopwatch_seconds);
        const char *actions[]{s.clock.stopwatch_running ? "PAUSE" : s.clock.stopwatch_seconds ? "RESUME" : "START","RESET","BACK"};
        card(surface,"STOPWATCH",elapsed,s.clock.stopwatch_running ? "RUNNING" : "PAUSED",actions[s.clock_index%3],s.clock_index,3); return;
    }
    if (s.screen==Screen::ClockOptions) {
        constexpr const char *titles[]{"TIME FORMAT","FACE GLANCE","USB DESK","BACK"};
        const auto i=s.clock_index%4;
        const auto &options=s.clock.settings;
        const char *value=i==0 ? options.hour24 ? "24 H" : "12 H" : i==1 ? options.glance ? "ON" : "OFF" :
            i==2 ? options.usb_desk ? "ON" : "OFF" : "CLOCK";
        card(surface,titles[i],value,"",i==3 ? "BACK" : "TOGGLE",i,4); return;
    }
    if (s.screen==Screen::Alarms) {
        const auto i=s.clock_index%5;
        if (i==4) { card(surface,"ALARMS","CLOCK","","BACK",i,5); return; }
        const auto &a=s.clock.settings.alarms[i];
        char value[16],title[24],context[24];
        std::snprintf(value,sizeof(value),"%02u:%02u",a.minute/60,a.minute%60);
        std::snprintf(title,sizeof(title),"ALARM %u / %s",i+1,a.enabled ? "ON" : "OFF");
        const char *repeat=a.weekdays==0 ? "ONCE" : a.weekdays==127 ? "DAILY" : a.weekdays==31 ? "WEEKDAYS" : a.weekdays==96 ? "WEEKEND" : "WEEKLY";
        std::snprintf(context,sizeof(context),"%s",s.clock.valid ? repeat : "TIME UNSET");
        card(surface,title,value,context,"CHANGE",i,5); return;
    }
    constexpr const char *actions[]{"DECREASE","INCREASE","NEXT FIELD","SAVE","CANCEL"};
    char value[24],title[24],context[24]{};
    if (s.screen==Screen::ClockEdit) {
        constexpr const char *fields[]{"YEAR","MONTH","DAY","HOUR","MINUTE","UTC OFFSET"};
        const unsigned values[]{s.clock_draft.year,s.clock_draft.month,s.clock_draft.day,s.clock_draft.hour,s.clock_draft.minute};
        const auto field=s.clock_edit_field%6;
        if (field<5) std::snprintf(value,sizeof(value),"%02u",values[field]);
        else std::snprintf(value,sizeof(value),"%+d MIN",s.clock_offset_draft);
        std::snprintf(title,sizeof(title),"SET %s",fields[field]);
        std::snprintf(context,sizeof(context),"%u/6",field+1);
    } else {
        const auto &a=s.alarm_draft;
        const auto field=s.clock_edit_field%4;
        constexpr const char *fields[]{"HOUR","MINUTE","REPEAT","ENABLED"};
        std::snprintf(title,sizeof(title),"ALARM %u %s",s.alarm_index+1,fields[field]);
        if (field<2) std::snprintf(value,sizeof(value),"%02u",field==0 ? a.minute/60 : a.minute%60);
        else if (field==3) std::snprintf(value,sizeof(value),"%s",a.enabled ? "ON" : "OFF");
        else {
            const char *name=a.weekdays==0 ? "ONCE" : a.weekdays==127 ? "DAILY" : a.weekdays==31 ? "WEEKDAYS" : a.weekdays==96 ? "WEEKEND" : "CUSTOM";
            constexpr const char *days[]{"MON","TUE","WED","THU","FRI","SAT","SUN"};
            for (unsigned i=0;i<7;++i) if (a.weekdays==(1U<<i)) name=days[i];
            std::snprintf(value,sizeof(value),"%s",name);
        }
        std::snprintf(context,sizeof(context),"%u/4",field+1);
    }
    card(surface,title,value,context,actions[s.clock_action%5],s.clock_action,5);
}

void render_screen(const State &state, MonoSurface &surface)
{
    if (state.timer_completed) {
        render_face(state, surface);
        centered(surface, state.focus_finished ? "SESSION DONE" : state.focus_phase == FocusPhase::Work ? "WORK DONE" :
                      state.focus_phase == FocusPhase::Break ? "BREAK DONE" : "DONE",surface.height-11U,2);
        return;
    }
    if (state.clock.ringing) {
        State alert=state; alert.mood=Mood::Alert; alert.eyes_closed=false;
        render_face(alert,surface);
        centered(surface,state.audio_ready ? "ALARM / DISMISS" : "ALARM / NO SOUND",surface.height-11U,2);
        return;
    }
    if (state.finding) {
        State searching=state; searching.mood=Mood::Curious;
        render_face(searching,surface);
        centered(surface,"PICO HERE",surface.height-11U,2);
        return;
    }
    if (state.screen==Screen::Pairing || state.pairing.active()) {
        const auto &pair=state.pairing;
        char value[16]{};
        if (pair.stage==PairingStage::Passkey) std::snprintf(value,sizeof(value),"%06u",static_cast<unsigned>(pair.passkey));
        else std::snprintf(value,sizeof(value),"%s",pair.stage==PairingStage::Confirm ? "READY?" :
            pair.stage==PairingStage::Waiting ? "PAIRING" : pair.stage==PairingStage::Paired ? "PAIRED" :
            pair.stage==PairingStage::TimedOut ? "EXPIRED" : "FAILED");
        char context[24]{};
        if (pair.stage==PairingStage::Failed) std::snprintf(context,sizeof(context),"ERROR %u",pair.error);
        else std::snprintf(context,sizeof(context),"%s",pair.stage==PairingStage::Passkey ? "ENTER ON PHONE" :
            pair.stage==PairingStage::Waiting ? "CONNECT IN APP" : "");
        card(surface,"PAIR PHONE",value,context,pair.stage==PairingStage::Confirm ? "HOLD ACTION" : "BACK");
        return;
    }
    if (state.screen==Screen::PairingRecovery) {
        const auto status=state.pairing_reset;
        card(surface,"PAIR AGAIN",status==PairingResetState::Cleared ? "CLEARED" :
            status==PairingResetState::Failed ? "FAILED" : status==PairingResetState::Pending ? "WAIT" : "FORGET?",
            status==PairingResetState::Confirm ? "ALL PHONES" : status==PairingResetState::Cleared ? "PAIR IN APP" : "",
            status==PairingResetState::Confirm ? "HOLD ACTION" : "BACK");
        return;
    }
    if (state.screen>=Screen::Clock) { render_clock_utility(state,surface); return; }
    if (state.screen == Screen::Home || state.screen == Screen::Themes) {
        if (state.screen==Screen::Home && state.clock.settings.usb_desk && state.clock.usb_powered && state.mood!=Mood::Happy) {
            clock_card(state,surface,true); return;
        }
        render_face(state, surface);
        if (state.screen == Screen::Home) render_status(state, surface);
        else {
            centered(surface, assets::theme_names[static_cast<unsigned>(state.theme) % 8],surface.height-11U,2);
        }
        return;
    }
    if (state.screen == Screen::Settings) {
        constexpr const char *names[]{"FACE","MUTE","VOLUME","WORK TIME","BREAK TIME","MOTION","TEST SOUND","BRIGHTNESS","SCREEN TIME","SLEEP NOW","BACK","PAIR AGAIN","PAIR PHONE"};
        const auto i=state.settings_index%13U;
        char value[24]{};
        if (i==0) std::snprintf(value,sizeof(value),"%s",assets::theme_names[static_cast<unsigned>(state.theme)%8]);
        else if (i==1) std::snprintf(value,sizeof(value),"%s",state.sound_muted ? "ON" : "OFF");
        else if (i==2 || i==7) std::snprintf(value,sizeof(value),"%u%%",i==2 ? state.sound_volume_percent : state.display_brightness_percent);
        else if (i==3 || i==4) duration(value,sizeof(value),i==3 ? state.configured_duration_seconds : state.break_duration_seconds);
        else if (i==5) std::snprintf(value,sizeof(value),"%s",state.motion_config ? "ON" : "OFF");
        else if (i==8) {
            if (state.display_timeout_minutes) std::snprintf(value,sizeof(value),"%u MIN",state.display_timeout_minutes);
            else std::snprintf(value,sizeof(value),"OFF");
        }
        card(surface,"SETTINGS",names[i],value,i==9 ? "SLEEP" : i==10 ? "BACK" : i>=11 ? "OPEN" : i==6 ? "PLAY" : "CHANGE",i,13);
        return;
    }
    if (state.screen == Screen::SettingEdit) {
        constexpr const char *actions[]{"DECREASE","INCREASE","SAVE","CANCEL"};
        const auto action=state.setting_action%4U;
        if (state.setting_key == SettingKey::Theme) {
            const auto index=state.setting_draft%8;
            centered(surface,assets::theme_names[index],0,2);
            const auto &face=assets::faces[index][0];
            bitmap(surface,face.brow_left,42,15); bitmap(surface,face.brow_right,86,15);
            bitmap(surface,face.left,42,28); bitmap(surface,face.right,86,28);
            bitmap(surface,face.mouth,64,42);
            selected_action(surface,actions[action],action,4);
        } else {
            char value[24]{};
            const char *title="VOLUME";
            switch (state.setting_key) {
            case SettingKey::Mute: title="MUTE"; std::snprintf(value,sizeof(value),"%s",state.setting_draft ? "ON" : "OFF"); break;
            case SettingKey::Motion: {
                title="MOTION";
                constexpr const char *masks[]{"OFF","WAKE","ROCK","WAKE ROCK","REST","WAKE REST","ROCK REST","ALL ON"};
                std::snprintf(value,sizeof(value),"%s",masks[state.setting_draft%8]); break;
            }
            case SettingKey::DisplayTimeout:
                title="SCREEN TIME";
                if (state.setting_draft) std::snprintf(value,sizeof(value),"%u MIN",state.setting_draft);
                else std::snprintf(value,sizeof(value),"OFF");
                break;
            case SettingKey::FocusGoal:
                title="ROUND GOAL";
                if (state.setting_draft) std::snprintf(value,sizeof(value),"%u",state.setting_draft);
                else std::snprintf(value,sizeof(value),"CONT");
                break;
            default:
                title=state.setting_key == SettingKey::Brightness ? "BRIGHTNESS" : "VOLUME";
                std::snprintf(value,sizeof(value),"%u%%",state.setting_draft); break;
            }
            card(surface,title,value,"",actions[action],action,4);
        }
        return;
    }
    if (state.screen == Screen::DurationEdit) {
        char value[24]; std::snprintf(value,sizeof(value),"%u MIN",state.draft_duration_seconds/60U);
        constexpr const char *actions[]{"-1 MIN","+1 MIN","SAVE","CANCEL"};
        const auto i=state.duration_action%4U;
        card(surface,state.editing_break_duration ? "BREAK TIME" : state.mode == Mode::Cooking ? "TIMER TIME" : "WORK TIME",value,"",actions[i],i,4);
        return;
    }
    if (state.screen == Screen::Tuner) {
        char value[16]="--",context[24]{};
        if (!state.microphone_ready) std::snprintf(value,sizeof(value),"NO MIC");
        else if (!state.tuner_running) std::snprintf(context,sizeof(context),"STOPPED");
        else if (state.tuner.signal == pitch::Signal::Detected) {
            std::snprintf(value,sizeof(value),"%s%d",pitch::note_name(state.tuner.note),static_cast<int>(state.tuner.note)/12-1);
            const int cents=state.tuner.cents;
            if (cents>=-5 && cents<=5) std::snprintf(context,sizeof(context),"IN TUNE");
            else std::snprintf(context,sizeof(context),"%s %d",cents<0 ? "FLAT" : "SHARP",cents<0 ? -cents : cents);
        } else std::snprintf(context,sizeof(context),"%s",state.tuner.signal == pitch::Signal::Quiet ? "TOO QUIET" :
            state.tuner.signal == pitch::Signal::Clipping ? "TOO LOUD" : state.tuner.signal == pitch::Signal::Noisy ? "ONE NOTE" : "PLUCK A NOTE");
        card(surface,"TUNER",value,context,state.tuner_running ? "STOP" : "LISTEN");
        return;
    }
    if (state.screen == Screen::ReferenceTone) {
        char value[16],frequency[24];
        std::snprintf(value,sizeof(value),"%s%d",pitch::note_name(state.reference_note),static_cast<int>(state.reference_note)/12-1);
        const auto hz=audio::frequency_millihz(state.reference_note);
        const auto tenths=(hz+50U)/100U;
        std::snprintf(frequency,sizeof(frequency),"%u.%u HZ",tenths/10U,tenths%10U);
        const char *actions[]{"NOTE -","NOTE +",state.reference_playing ? "STOP" : "PLAY","OCTAVE -","OCTAVE +","BACK"};
        const auto i=state.reference_index%6U;
        card(surface,!state.audio_ready ? "NO SPEAKER" : state.sound_muted || !state.sound_volume_percent ? "MUTED" :
            state.reference_playing ? "PLAYING" : "REFERENCE",value,frequency,actions[i],i,6);
        return;
    }
    if (state.screen == Screen::Music) {
        char value[16],context[24];
        std::snprintf(value,sizeof(value),"%u",state.metronome_bpm);
        if (state.metronome_running) std::snprintf(context,sizeof(context),"BEAT %u/%u",state.current_beat,state.beats_per_bar);
        else std::snprintf(context,sizeof(context),"%u BEATS",state.beats_per_bar);
        const char *actions[]{"BPM -","BPM +",state.metronome_running ? "STOP" : "START","TAP TEMPO","BEATS +","TUNER","REFERENCE","HOME"};
        const auto i=state.music_index%8U;
        card(surface,state.sound_muted || !state.sound_volume_percent ? "BPM / MUTED" : "BPM",value,context,actions[i],i,8);
        if (state.metronome_running && state.beat_pulse) fill_rect(surface,32,35,64,2);
        return;
    }
    if (state.screen == Screen::Menu) {
        const auto selected = static_cast<unsigned>(state.menu_index) % assets::menu_icons.size();
        const auto &icon = assets::menu_icons[selected];
        const bool large = surface.width >= 96 && surface.height >= 56;
        const unsigned scale = large ? 2U : 1U;
        const unsigned left = (surface.width-16U*scale)/2U, top = large ? 5U : 1U;
        for (unsigned y=0; y<16; ++y)
            for (unsigned x=0; x<16; ++x)
                if (icon.rows[y] & (0x8000U >> x))
                    fill_rect(surface,left+x*scale,top+y*scale,scale,scale);
        const auto label_width = static_cast<unsigned>(std::strlen(icon.label))*4U*scale-scale;
        text(surface,icon.label,(surface.width-label_width)/2U,large ? 44U : 19U,scale);
        // Carousel chevrons and a quiet position indicator; no animation loop.
        const unsigned arrow_y = top+8U*scale-4U;
        for (unsigned i=0; i<5; ++i) {
            const unsigned offset = i < 3 ? 2-i : i-2;
            fill_rect(surface,5+offset,arrow_y+i*2,2,2);
            fill_rect(surface,surface.width-9-offset,arrow_y+i*2,2,2);
        }
        const unsigned first = (surface.width-(assets::menu_icons.size()-1U)*6U-3U)/2U;
        for (unsigned i=0; i<assets::menu_icons.size(); ++i) {
            const unsigned x=first+i*6U, y=surface.height-4U;
            if (i == selected) fill_rect(surface,x,y,3,3);
            else fill_rect(surface,x+1,y+1,1,1);
        }
        return;
    }
    if (state.screen == Screen::FocusSummary) {
        if (!state.focus_history_count) card(surface,"HISTORY / BOOT","EMPTY","","BACK");
        else {
            const auto i=state.focus_history_index%state.focus_history_count;
            const auto &record=state.focus_history[i];
            char title[24],time[24],context[24];
            std::snprintf(title,sizeof(title),"%u/%u THIS BOOT",i+1,state.focus_history_count);
            duration(time,sizeof(time),record.work_seconds);
            std::snprintf(context,sizeof(context),"%s %u",record.finished ? "DONE:" : "STOPPED:",record.rounds);
            card(surface,title,time,context,"BACK",i,state.focus_history_count);
        }
        return;
    }
    if (state.screen == Screen::Unavailable) {
        card(surface,"UNAVAILABLE","NOT YET","","BACK"); return;
    }
    // Focus/Cooking keep their existing actions; only the selected one is shown.
    const bool focus=state.mode == Mode::Focus;
    const auto count=focus ? 8U : 4U;
    const auto i=state.utility_index%count;
    char value[24],context[24]{};
    const char *title=focus ? "FOCUS" : "COOKING";
    duration(value,sizeof(value),state.timer_running || state.timer_paused ? state.timer_remaining_seconds : state.configured_duration_seconds);
    const char *actions[]{focus ? "WORK TIME" : "TIMER TIME",state.timer_running ? "PAUSE" : state.timer_paused ? "RESUME" : "START TIMER",
        "CANCEL","HOME",state.focus_waiting ? "NEXT PHASE" : state.focus_phase != FocusPhase::None ? "CYCLE ACTIVE" : "START CYCLE",
        "BREAK TIME","ROUND GOAL","HISTORY"};
    if (i==0 || i==5) {
        title=i==5 ? "BREAK TIME" : focus ? "WORK TIME" : "TIMER TIME";
        duration(value,sizeof(value),i==5 ? state.break_duration_seconds : state.configured_duration_seconds);
    } else if (i==6) {
        title="ROUND GOAL";
        if (state.focus_goal) std::snprintf(value,sizeof(value),"%u",state.focus_goal);
        else std::snprintf(value,sizeof(value),"CONT");
        std::snprintf(context,sizeof(context),"NEXT SESSION");
    } else if (i==7) {
        title="HISTORY"; std::snprintf(value,sizeof(value),"%u",state.focus_history_count);
        std::snprintf(context,sizeof(context),"THIS BOOT");
    } else if (state.focus_waiting) {
        std::snprintf(value,sizeof(value),"READY");
        std::snprintf(context,sizeof(context),"%s",state.focus_phase == FocusPhase::Work ? "BREAK NEXT" : "WORK NEXT");
    } else if (state.timer_paused) std::snprintf(context,sizeof(context),"PAUSED");
    else if (focus && state.focus_phase != FocusPhase::None) {
        title=state.focus_phase == FocusPhase::Break ? "BREAK" : "FOCUS";
        if (state.active_focus_goal) std::snprintf(context,sizeof(context),"%u/%u DONE",state.completed_work_sessions,state.active_focus_goal);
        else std::snprintf(context,sizeof(context),"%u DONE",state.completed_work_sessions);
    } else std::snprintf(context,sizeof(context),"%s",state.timer_running ? "RUNNING" : "READY");
    card(surface,title,value,context,actions[i],i,count);
    if ((state.timer_running || state.timer_paused) && state.timer_total_seconds && surface.height>=56)
        fill_rect(surface,4,48,static_cast<unsigned>(static_cast<std::uint64_t>(state.timer_total_seconds-state.timer_remaining_seconds)*120U/state.timer_total_seconds),1);

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
    if (!state.display_asleep) render_screen(state, surface);
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
