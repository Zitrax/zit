#pragma once

#include <memory>

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace zit {

/**
 * Get and initialize logger.
 *
 * Note that main.cpp currently can set the log level using --log-level or
 * the level can be set using the environment variable SPDLOG_LEVEL.
 */
inline auto logger(const std::string& name = "console") {
  auto logger = spdlog::get(name);
  if (!logger) {
    logger = spdlog::stderr_color_mt(name);
    spdlog::cfg::load_env_levels();
  }
  return logger;
}

}  // namespace zit
