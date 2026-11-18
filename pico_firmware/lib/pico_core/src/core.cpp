#include <pico/core.hpp>

#include <pico/features.hpp>

#include <limits>

namespace pico {
namespace {

constexpr std::uint32_t kHappyDurationSeconds = 4;

bool valid_mode(Mode mode)
{
    return static_cast<std::uint8_t>(mode) < static_cast<std::uint8_t>(Mode::Count);
}

bool valid_mood(Mood mood)
{
    return static_cast<std::uint8_t>(mood) < static_cast<std::uint8_t>(Mood::Count);
}

bool valid_dock(Dock dock)
{
    return static_cast<std::uint8_t>(dock) < static_cast<std::uint8_t>(Dock::Count);
}
bool valid_theme(FaceTheme theme)
{
    return static_cast<std::uint8_t>(theme) <
           static_cast<std::uint8_t>(FaceTheme::Count);
}

std::uint32_t saturating_add(std::uint32_t lhs, std::uint32_t rhs)
{
    if (rhs > std::numeric_limits<std::uint32_t>::max() - lhs) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return lhs + rhs;
}

} // namespace

Effects Core::set_theme(FaceTheme theme)
{
    if (!valid_theme(theme) || theme == state_.theme) {
        return effect(Effect::None);
    }

    state_.theme = theme;
    return Effect::Render | Effect::Notify | Effect::Persist;
}

Effects Core::handle(const Event &event)
{
    switch (event.kind) {
    case EventKind::Boot:
        return Effect::Render | Effect::Notify;

    case EventKind::Tick:
        return on_tick(event.value);

    case EventKind::PrimaryPressed:
        state_.interaction_count = saturating_add(state_.interaction_count, 1U);
        state_.mood = Mood::Happy;
        mood_reset_deadline_seconds_ = saturating_add(state_.uptime_seconds,
                                                       kHappyDurationSeconds);
        return Effect::Render | Effect::Notify | Effect::Persist | Effect::Chirp;

    case EventKind::SecondaryPressed:
        if (state_.dock == Dock::Car) {
            return effect(Effect::None);
        }
        return set_mode(next_user_mode(state_.mode));

    case EventKind::FocusButtonTapped: {
        // A tap during any running timer is a pat; it never restarts the timer.
        if (state_.timer_running) {
            return handle({EventKind::PrimaryPressed, 0});
        }
        if (state_.dock == Dock::Car) {
            return effect(Effect::None);
        }
        Effects effects = set_mode(Mode::Focus);
        effects |= handle({EventKind::StartTimer, 25U * 60U});
        effects |= handle({EventKind::PrimaryPressed, 0});
        return effects;
    }

    case EventKind::FocusButtonHeld: {
        if (state_.dock == Dock::Car || state_.mode != Mode::Focus ||
            !state_.timer_running) {
            return effect(Effect::None);
        }
        Effects effects = handle({EventKind::CancelTimer, 0});
        effects |= set_mode(Mode::Companion);
        effects |= set_mood(Mood::Neutral);
        return effects;
    }

    case EventKind::Connected:
        if (state_.connected) {
            return effect(Effect::None);
        }
        state_.connected = true;
        return Effect::Render | Effect::Notify;

    case EventKind::Disconnected:
        if (!state_.connected) {
            return effect(Effect::None);
        }
        state_.connected = false;
        return Effect::Render | Effect::Notify;

    case EventKind::DockChanged: {
        const auto dock = static_cast<Dock>(event.value);
        if (!valid_dock(dock) || dock == state_.dock) {
            return effect(Effect::None);
        }

        const auto previous_dock = state_.dock;
        state_.dock = dock;

        if (dock == Dock::Car) {
            if (previous_dock != Dock::Car) {
                mode_before_car_dock_ = state_.mode;
            }
            state_.mode = Mode::Driving;
        } else if (previous_dock == Dock::Car && state_.mode == Mode::Driving) {
            state_.mode = valid_mode(mode_before_car_dock_)
                              ? mode_before_car_dock_
                              : Mode::Companion;
        }

        return Effect::Render | Effect::Notify | Effect::Persist;
    }

    case EventKind::SetTheme:
        if (event.value >= static_cast<std::uint32_t>(FaceTheme::Count)) {
            return effect(Effect::None);
        }
        return set_theme(static_cast<FaceTheme>(event.value));

    case EventKind::CycleTheme: {
        const auto current = static_cast<std::uint8_t>(state_.theme);
        const auto theme_count =
            static_cast<std::uint8_t>(FaceTheme::Count);

        const auto next =
            static_cast<std::uint8_t>((current + 1U) % theme_count);

        return set_theme(static_cast<FaceTheme>(next));
    }

    case EventKind::SetMode:
        return set_mode(static_cast<Mode>(event.value));

    case EventKind::SetMood:
        return set_mood(static_cast<Mood>(event.value));

    case EventKind::SetBattery: {
        const auto percentage = static_cast<std::uint8_t>(event.value);
        if ((percentage > 100U && percentage != kBatteryUnknown) ||
            percentage == state_.battery_percent) {
            return effect(Effect::None);
        }
        state_.battery_percent = percentage;
        return Effect::Render | Effect::Notify;
    }

    case EventKind::StartTimer:
        if (event.value == 0U || event.value > kMaxTimerSeconds) {
            return effect(Effect::None);
        }
        state_.timer_running = true;
        state_.timer_total_seconds = event.value;
        state_.timer_remaining_seconds = event.value;
        timer_deadline_seconds_ = saturating_add(state_.uptime_seconds, event.value);
        return Effect::Render | Effect::Notify;

    case EventKind::CancelTimer:
        if (!state_.timer_running) {
            return effect(Effect::None);
        }
        state_.timer_running = false;
        state_.timer_total_seconds = 0;
        state_.timer_remaining_seconds = 0;
        timer_deadline_seconds_ = 0;
        return Effect::Render | Effect::Notify;

    case EventKind::RequestStatus:
        return effect(Effect::Notify);
    }

    return effect(Effect::None);
}

Effects Core::set_mode(Mode mode)
{
    if (!valid_mode(mode) || mode == Mode::Driving || state_.dock == Dock::Car ||
        mode == state_.mode) {
        return effect(Effect::None);
    }

    state_.mode = mode;
    return Effect::Render | Effect::Notify | Effect::Persist;
}

Effects Core::set_mood(Mood mood)
{
    if (!valid_mood(mood) || mood == state_.mood) {
        return effect(Effect::None);
    }

    state_.mood = mood;
    mood_reset_deadline_seconds_ = 0;
    return Effect::Render | Effect::Notify | Effect::Persist;
}

Effects Core::on_tick(std::uint32_t now_seconds)
{
    state_.uptime_seconds = now_seconds;
    Effects effects = effect(Effect::None);

    if (state_.timer_running) {
        const auto remaining = now_seconds >= timer_deadline_seconds_
                                   ? 0U
                                   : timer_deadline_seconds_ - now_seconds;
        if (remaining != state_.timer_remaining_seconds) {
            state_.timer_remaining_seconds = remaining;
            effects |= effect(Effect::Render);
            effects |= effect(Effect::Notify);
        }

        if (remaining == 0U) {
            state_.timer_running = false;
            state_.mood = Mood::Alert;
            mood_reset_deadline_seconds_ = saturating_add(now_seconds,
                                                           kHappyDurationSeconds);
            effects |= effect(Effect::Chirp);
        }
    }

    if (mood_reset_deadline_seconds_ != 0U &&
        now_seconds >= mood_reset_deadline_seconds_) {
        mood_reset_deadline_seconds_ = 0;
        if (state_.mood == Mood::Happy || state_.mood == Mood::Alert) {
            state_.mood = Mood::Neutral;
            effects |= effect(Effect::Render);
            effects |= effect(Effect::Notify);
        }
    }

    return effects;
}

PersistedState Core::persistent_state() const
{
    PersistedState persisted{};

    persisted.mode =
        state_.mode == Mode::Driving
            ? mode_before_car_dock_
            : state_.mode;

    persisted.mood =
        state_.mood == Mood::Happy ||
        state_.mood == Mood::Alert
            ? Mood::Neutral
            : state_.mood;

    persisted.theme = state_.theme;
    persisted.interaction_count = state_.interaction_count;

    return persisted;
}
bool Core::restore(const PersistedState &persisted)
{
    if (!valid_mode(persisted.mode) ||
        persisted.mode == Mode::Driving ||
        !valid_mood(persisted.mood) ||
        !valid_theme(persisted.theme)) {
        return false;
    }

    state_.mode = persisted.mode;
    state_.mood = persisted.mood;
    state_.theme = persisted.theme;
    state_.interaction_count = persisted.interaction_count;
    mode_before_car_dock_ = persisted.mode;

    return true;
}
} // namespace pico
