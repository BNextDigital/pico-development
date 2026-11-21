#pragma once
#include <cstdint>
struct k_mem_slab;
namespace pico::platform {
void profile_sample(std::uint32_t queue_used, std::uint32_t queue_peak, std::uint32_t queue_drops);
void profile_slab(const char *name, k_mem_slab *slab);
void profile_audio();
void profile_microphone();
}
