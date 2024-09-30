#pragma once

#include <chrono>

using local_clock = std::chrono::system_clock;
using time_point = std::chrono::time_point<local_clock>;