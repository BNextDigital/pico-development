#pragma once
#include <cstdint>
#include <type_traits>

// Reproduce the SDK's unsigned-long limit macro on the desktop compiler.
// This tests template deduction, not the ARM ABI or a complete Zephyr build.
#undef UINT32_MAX
#define UINT32_MAX 0xffffffffUL
static_assert(std::is_same<decltype(UINT32_MAX), unsigned long>::value,
              "Regression profile must use the Nordic UINT32_MAX macro type");
