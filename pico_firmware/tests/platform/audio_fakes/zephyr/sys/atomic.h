#pragma once
using atomic_t = int;
using atomic_val_t = int;
inline int atomic_get(const atomic_t *value) { return *value; }
inline int atomic_set(atomic_t *value,int next) { const auto old=*value; *value=next; return old; }
inline int atomic_clear(atomic_t *value) { return atomic_set(value,0); }
inline int atomic_inc(atomic_t *value) { return (*value)++; }
