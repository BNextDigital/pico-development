#pragma once
#include <cstddef>
#define BIT(n) (1U << (n))
#define CONTAINER_OF(ptr, type, member) reinterpret_cast<type *>(reinterpret_cast<char *>(ptr) - offsetof(type, member))
