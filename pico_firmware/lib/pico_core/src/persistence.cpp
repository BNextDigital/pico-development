#include <pico/persistence.hpp>

namespace pico::persistence {
namespace {

std::uint32_t read_u32_le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

void write_u32_le(std::uint32_t value, std::uint8_t *output)
{
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
    output[2] = static_cast<std::uint8_t>(value >> 16U);
    output[3] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t fnv1a(const std::uint8_t *data, std::size_t size)
{
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619U;
    }
    return hash;
}

} // namespace

Blob encode(const PersistedState &state)
{
    Blob data{};
    data[0] = 'P';
    data[1] = 'I';
    data[2] = 'C';
    data[3] = 'O';
    data[4] = kStorageVersion;
    data[5] = static_cast<std::uint8_t>(state.mode);
    data[6] = static_cast<std::uint8_t>(state.mood);
    data[7] = static_cast<std::uint8_t>(state.theme);
    write_u32_le(state.interaction_count, &data[8]);
    write_u32_le(state.configured_duration_seconds, &data[12]);
    write_u32_le(fnv1a(data.data(), 16), &data[16]);
    return data;
}

bool decode(const std::uint8_t *data, std::size_t size, PersistedState &state)
{
    if (!data || size < 5) return false;
    const auto version = data[4];
    const auto expected = version == 1 || version == 2 ? 16U : version == 3 ? 20U : 0U;
    if (!expected || size != expected || data[0] != 'P' || data[1] != 'I' ||
        data[2] != 'C' || data[3] != 'O' ||
        read_u32_le(data + expected - 4) != fnv1a(data, expected - 4)) return false;
    const auto mode = static_cast<Mode>(data[5]);
    const auto mood = static_cast<Mood>(data[6]);
    const auto theme = version >= 2 ? static_cast<FaceTheme>(data[7]) : FaceTheme::Modern;
    const auto duration = version >= 3 ? read_u32_le(data + 12) : 1500U;
    if (mode >= Mode::Count || mode == Mode::Driving || mood >= Mood::Count ||
        theme >= FaceTheme::Count || !duration || duration > kMaxTimerSeconds) return false;
    state = {mode, mood, read_u32_le(data + 8), theme, duration};
    return true;
}
} // namespace pico::persistence
