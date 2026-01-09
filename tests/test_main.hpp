// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <functional>

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern std::function<void(int)> sigint_function;
