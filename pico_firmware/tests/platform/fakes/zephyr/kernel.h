#pragma once
#include <cstdint>
struct k_work { void (*handler)(k_work *); };
struct k_work_delayable { k_work work{}; bool pending{false}; std::uint64_t due{0}; };
using k_timeout_t = std::uint32_t;
#define K_NO_WAIT 0U
#define K_MSEC(ms) (ms)
void k_work_init_delayable(k_work_delayable *, void (*)(k_work *));
int k_work_schedule(k_work_delayable *, k_timeout_t);
int k_work_reschedule(k_work_delayable *, k_timeout_t);
k_work_delayable *k_work_delayable_from_work(k_work *);
std::uint32_t k_uptime_get_32();
