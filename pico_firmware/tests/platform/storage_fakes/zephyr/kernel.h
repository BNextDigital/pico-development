#pragma once
#include <cstdint>
struct k_work {};
struct k_work_delayable { void (*handler)(k_work *); unsigned delay{}; };
struct k_mutex {};
#define K_MUTEX_DEFINE(name) k_mutex name
#define K_WORK_DELAYABLE_DEFINE(name, fn) k_work_delayable name{fn,0}
#define K_FOREVER 0
#define K_SECONDS(x) (x)
inline void k_mutex_lock(k_mutex *, int) {}
inline void k_mutex_unlock(k_mutex *) {}
inline int k_work_reschedule(k_work_delayable *work, unsigned delay) { work->delay=delay; return 0; }
