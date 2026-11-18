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
    write_u32_le(fnv1a(data.data(), 12), &data[12]);
    return data;
}

bool decode(const Blob &data, PersistedState &state)
{
    const auto version = data[4];

    if (data[0] != 'P' ||
        data[1] != 'I' ||
        data[2] != 'C' ||
        data[3] != 'O' ||
        (version != 1U && version != kStorageVersion) ||
        read_u32_le(&data[12]) != fnv1a(data.data(), 12)) {
        return false;
    }

    const auto mode = static_cast<Mode>(data[5]);
    const auto mood = static_cast<Mood>(data[6]);

    FaceTheme theme = FaceTheme::Modern;

    if (version >= 2U) {
        theme = static_cast<FaceTheme>(data[7]);

        if (static_cast<std::uint8_t>(theme) >=
            static_cast<std::uint8_t>(FaceTheme::Count)) {
            return false;
        }
    }

    if (mode >= Mode::Count ||
        mode == Mode::Driving ||
        mood >= Mood::Count) {
        return false;
    }

    state = {
        mode,
        mood,
        theme,
        read_u32_le(&data[8]),
    };

    return true;
}

} // namespace pico::persistence
// namespace pico::persistence
