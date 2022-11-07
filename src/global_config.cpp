#include "global_config.hpp"

#include "file_utils.hpp"
#include "string_utils.hpp"

#include "spdlog/fmt/ostr.h"  // Needed due to use of operator<<
#include "spdlog/spdlog.h"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

// NOLINTNEXTLINE(cert-dcl58-cpp)
namespace std::filesystem {
static std::ostream& operator<<(std::ostream& os, const path& p) {
  return os << p.string();
}
}  // namespace std::filesystem

namespace zit {

namespace {

std::string getenv(const char* env) {
  // We don't expect any other thread to change the env so suppress
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* val = std::getenv(env);
  return val ? val : "";
}

// Return a list of candidate config directories in precedence order
std::vector<fs::path> config_dirs(spdlog::logger& logger) {
#ifndef __linux__
  logger.warn("Config dirs not yet implemented for non linux");
#endif

  std::vector<fs::path> dirs;

  const auto home_dir = getenv("HOME");

  auto config_dir = getenv("XDG_CONFIG_HOME");
  if (fs::exists(config_dir)) {
    dirs.emplace_back(config_dir);
  } else if (fs::exists(home_dir)) {
    dirs.emplace_back(fs::path(home_dir) / ".local" / "share");
  }

  config_dir = getenv("XDG_CONFIG_DIRS");
  for (const auto& dir : split(config_dir, ":")) {
    if (fs::exists(dir)) {
      dirs.emplace_back(dir);
    }
  }

  // As a last resort directly in the home directory
  if (fs::exists(home_dir)) {
    dirs.emplace_back(home_dir);
  }

  logger.debug("Config candidate dirs:");
  for (const auto& dir : dirs) {
    logger.debug("  {}", dir);
  }
  return dirs;
}

std::optional<bool> parse_bool(const std::string& _str) {
  const auto str = to_lower(_str);
  if (str == "true" || str == "1") {
    return true;
  }
  if (str == "false" || str == "0") {
    return false;
  }
  return {};
}

template <typename T>
const std::map<std::string, T> settings_map;

template <>
const std::map<std::string, BoolSetting> settings_map<BoolSetting>{
    {"initiate_peer_connections", BoolSetting::INITIATE_PEER_CONNECTIONS}};

}  // namespace

void FileConfig::update_value(const std::string& key,
                              const std::string& value) {
  if (settings_map<BoolSetting>.contains(key)) {
    auto parsed = parse_bool(value);
    if (!parsed) {
      m_logger->warn("{} = {} could not parsed as a boolean", key, value);
    } else {
      m_logger->debug("{} set to {}", key, *parsed);
      m_bool_settings[settings_map<BoolSetting>.at(key)] = *parsed;
    }
  } else {
    m_logger->warn("Unknown key '{}' in config file ignored", key);
  }
}

FileConfig::FileConfig() : m_logger(spdlog::get("console")) {
  // Read and apply config from disk
  for (const auto& config_dir : config_dirs(*m_logger)) {
    const auto config_file = config_dir / "zit" / ".zit";
    if (fs::exists(config_file)) {
      m_logger->info("Reading config from: {}", config_file);
      const auto config_str = read_file(config_file);
      for (const auto& line : split(config_str, "\n")) {
        m_logger->trace("line: {}", line);
        auto kv = split(line, "=");
        if (kv.size() != 2) {
          m_logger->warn("Ignoring invalid config line: {}", line);
        } else {
          trim(kv[0]);
          trim(kv[1]);
          update_value(kv[0], kv[1]);
        }
      }
      break;
    }
  }
}

}  // namespace zit
