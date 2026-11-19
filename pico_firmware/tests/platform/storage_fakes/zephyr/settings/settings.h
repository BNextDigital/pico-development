#pragma once
#include <cstddef>
#include <sys/types.h>
using settings_read_cb = ssize_t (*)(void *,void *,std::size_t);
int settings_subsys_init();
int settings_load();
int settings_save_one(const char *,const void *,std::size_t);
#define SETTINGS_STATIC_HANDLER_DEFINE(id, name, a, fn, b, c) [[maybe_unused]] static auto id = fn
