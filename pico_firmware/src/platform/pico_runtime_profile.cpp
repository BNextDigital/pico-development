#include "pico_runtime_profile.hpp"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_heap.h>
LOG_MODULE_REGISTER(pico_profile, CONFIG_PICO_LOG_LEVEL);
#if defined(CONFIG_COMMON_LIBC_MALLOC)
extern "C" int malloc_runtime_stats_get(struct sys_memory_stats *stats);
#endif
namespace pico::platform {
namespace {
void thread_sample(const k_thread *thread, void *)
{
    std::size_t unused = 0;
    const int error = k_thread_stack_space_get(thread, &unused);
    const char *name = k_thread_name_get(const_cast<k_thread *>(thread));
    k_thread_runtime_stats_t cpu{};
    const int cpu_error = k_thread_runtime_stats_get(const_cast<k_thread *>(thread), &cpu);
    LOG_INF("thread=%p name=%s stack_size=%zu unused_min=%zu stack_err=%d cycles=%llu cpu_err=%d",
        thread, name ? name : "unnamed", thread->stack_info.size, unused, error,
        static_cast<unsigned long long>(cpu.execution_cycles), cpu_error);
}
}
void profile_slab(const char *name, k_mem_slab *slab)
{
    LOG_INF("slab=%s blocks=%u used=%u peak=%u", name, slab->info.num_blocks,
        k_mem_slab_num_used_get(slab), k_mem_slab_max_used_get(slab));
}
void profile_sample(std::uint32_t used, std::uint32_t peak, std::uint32_t drops)
{
    LOG_INF("queue used=%u peak=%u drops=%u", used, peak, drops);
    k_thread_foreach_unlocked(thread_sample, nullptr);
    profile_audio();
    profile_microphone();
#if defined(CONFIG_COMMON_LIBC_MALLOC)
    sys_memory_stats heap{};
    const int error = malloc_runtime_stats_get(&heap);
    LOG_INF("libc_heap used=%zu peak=%zu free=%zu err=%d", heap.allocated_bytes,
        heap.max_allocated_bytes, heap.free_bytes, error);
#else
    LOG_INF("libc heap statistics unavailable for selected allocator");
#endif
    // PSA/mbedTLS private allocator is not covered by libc statistics.
}
}
