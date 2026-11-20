#pragma once
#include <cstdint>
struct k_mutex {};
#define K_MUTEX_DEFINE(name) k_mutex name
#define K_FOREVER 0
inline void k_mutex_lock(k_mutex *,int) {}
inline void k_mutex_unlock(k_mutex *) {}
inline std::int64_t k_uptime_get() { return 1000; }
inline std::uint32_t k_uptime_get_32() { return 1000; }
