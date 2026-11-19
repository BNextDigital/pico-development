#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <deque>
#include <vector>
#include <cerrno>
struct k_mem_slab { std::size_t bytes; unsigned used{0}; };
struct fake_queue { std::size_t bytes, depth; std::deque<std::vector<unsigned char>> entries{}; };
#define K_MEM_SLAB_DEFINE(name, bytes, count, align) k_mem_slab name{bytes}
#define K_MSGQ_DEFINE(name, bytes, depth, align) fake_queue name{bytes,depth,{}}
#define K_THREAD_DEFINE(name, stack, entry, a,b,c,prio,options,delay) [[maybe_unused]] static auto name = entry
#define K_MSEC(x) (x)
#define K_NO_WAIT 0
#define K_FOREVER -1
inline int k_mem_slab_alloc(k_mem_slab *slab,void **data,int) { *data=new unsigned char[slab->bytes]; ++slab->used; return 0; }
inline void k_mem_slab_free(k_mem_slab *slab,void *data) { delete[] static_cast<unsigned char *>(data); --slab->used; }
inline unsigned k_mem_slab_num_used_get(k_mem_slab *slab) { return slab->used; }
inline void k_msgq_purge(fake_queue *queue) { queue->entries.clear(); }
inline unsigned k_msgq_num_used_get(fake_queue *queue) { return queue->entries.size(); }
inline int k_msgq_put(fake_queue *queue,const void *data,int) {
 if(queue->entries.size()>=queue->depth) return -ENOMSG;
 const auto *p=static_cast<const unsigned char *>(data); queue->entries.emplace_back(p,p+queue->bytes); return 0;
}
inline int k_msgq_get(fake_queue *queue,void *data,int) {
 if(queue->entries.empty()) return -ENOMSG;
 std::memcpy(data,queue->entries.front().data(),queue->bytes); queue->entries.pop_front(); return 0;
}
std::int64_t k_uptime_get();
void k_msleep(unsigned);
