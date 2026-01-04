#pragma once

#include <memory>

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace zit {

enum class LogOutput : uint8_t {
  CONSOLE,  //!< Standard console output
  MEMORY,   //!< In-memory log (for GUI display)
};

/**
 * Get and initialize logger.
 *
 * Note that main.cpp currently can set the log level using --log-level or
 * the level can be set using the environment variable SPDLOG_LEVEL.
 */
inline auto logger(const std::string& name = "zit",
                   LogOutput output = LogOutput::CONSOLE)
    -> std::shared_ptr<spdlog::logger> {
  auto logger = spdlog::get(name);
  if (!logger) {
    if (output == LogOutput::CONSOLE) {
      logger = spdlog::stderr_color_mt(name);
    } else if (output == LogOutput::MEMORY) {
      auto ringbuffer_sink =
          std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1000);
      logger = std::make_shared<spdlog::logger>(name, ringbuffer_sink);
      spdlog::register_logger(logger);
    } else {
      throw std::runtime_error("Unknown log output type");
    }
    spdlog::cfg::load_env_levels();
  }
  return logger;
}

}  // namespace zit
