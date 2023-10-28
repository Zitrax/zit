#pragma once

#include <cstdint>

namespace zit {

constexpr uint8_t MAJOR_VERSION{0};
constexpr uint8_t MINOR_VERSION{6};
constexpr uint8_t PATCH_VERSION{0};

// The peer id is currently two base 10 digits
static_assert(MAJOR_VERSION <= 99);
static_assert(MINOR_VERSION <= 99);
static_assert(PATCH_VERSION <= 99);

}  // namespace zit
