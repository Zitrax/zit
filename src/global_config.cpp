#include "global_config.hpp"

#include "file_utils.hpp"
#include "logger.hpp"
#include "string_utils.hpp"

#include <fmt/format.h>
#include <spdlog/common.h>
#include <spdlog/fmt/ostr.h>  // NOLINT(misc-include-cleaner) Needed due to use of operator<<

#ifndef _MSC_VER
#include <bits/basic_string.h>
#endif  // !_MSC_VER
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <KnownFolders.h>
#include <ShlObj.h>
#endif  // _WIN32

namespace fs = std::filesystem;

namespace zit {

namespace {

std::string getenv(const char* env) {
  // We don't expect any other thread to change the env so suppress
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* val = std::getenv(env);
  return val ? val : "";
}

// Return a list of candidate config directories in precedence order
//
// On Linux following the XDG Base Directory Specification (0.8)
//  https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
//
std::vector<fs::path> config_dirs() {
  std::vector<fs::path> dirs;

#ifdef _WIN32
  PWSTR wpath = nullptr;
  if (SUCCEEDED(
          SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &wpath))) {
    fs::path path{wpath};
    dirs.emplace_back(wpath);
    CoTaskMemFree(wpath);
  } else {
    logger()->warn("Failed to retrieve AppData path.");
  }
#elif __linux__

  const auto home_dir = getenv("HOME");

  // $XDG_CONFIG_HOME defines the base directory relative to which user-specific
  // configuration files should be stored. If $XDG_CONFIG_HOME is either not set
  // or empty, a default equal to $HOME/.config should be used.
  auto config_dir = getenv("XDG_CONFIG_HOME");
  if (config_dir.empty()) {
    dirs.emplace_back(fs::path(home_dir) / ".config");
  } else if (fs::exists(home_dir)) {
    dirs.emplace_back(config_dir);
  }

  // $XDG_CONFIG_DIRS defines the preference-ordered set of base directories to
  // search for configuration files in addition to the $XDG_CONFIG_HOME base
  // directory. The directories in $XDG_CONFIG_DIRS should be seperated with a
  // colon ':'.
  //
  // If $XDG_CONFIG_DIRS is either not set or empty, a value equal to /etc/xdg
  // should be used.
  config_dir = getenv("XDG_CONFIG_DIRS");
  if (config_dir.empty()) {
    dirs.emplace_back("etc/xdg");
  } else {
    for (const auto& dir : split(config_dir, ":")) {
      if (fs::exists(dir)) {
        dirs.emplace_back(dir);
      }
    }
  }

  // As a last resort directly in the home directory
  if (fs::exists(home_dir)) {
    dirs.emplace_back(home_dir);
  }

#else
  logger()->warn("Config dirs not yet implemented for this platform");
#endif

  logger()->debug("Config candidate dirs:");
  for (const auto& dir : dirs) {
    logger()->debug("  {}", dir);
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
    {"initiate_peer_connections", BoolSetting::INITIATE_PEER_CONNECTIONS},
    {"resolve_urls", BoolSetting::RESOLVE_URLS},
    {"piece_verify_threads", BoolSetting::PIECE_VERIFY_THREADS}};

template <>
const std::map<std::string, IntSetting> settings_map<IntSetting>{
    {"listening_port", IntSetting::LISTENING_PORT},
    {"connection_port", IntSetting::CONNECTION_PORT},
    {"retry_pieces_interval_seconds",
     IntSetting::RETRY_PIECES_INTERVAL_SECONDS},
    {"retry_peers_interval_seconds", IntSetting::RETRY_PEERS_INTERVAL_SECONDS}};

template <>
const std::map<std::string, StringSetting> settings_map<StringSetting>{
    {"bind_address", StringSetting::BIND_ADDRESS}};

}  // namespace

std::ostream& operator<<(std::ostream& os, const Config& config) {
  const auto dump = [&](const auto& settings) {
    for (const auto& [key, val] : settings) {
      os << key << "=" << config.get(val) << "\n";
    }
  };

  dump(settings_map<BoolSetting>);
  dump(settings_map<IntSetting>);
  return os;
}

FileConfig::FileConfig(std::filesystem::path config_file)
    : m_config_file(std::move(config_file)) {
  if (!m_config_file.empty() && !try_file(m_config_file)) {
    throw std::runtime_error(
        fmt::format("Could not read/use config file '{}'", m_config_file));
  }
}

bool FileConfig::try_file(const std::filesystem::path& config_file) {
  logger()->trace("Trying config file: {}", config_file);
  if (fs::exists(config_file)) {
    logger()->info("Reading config from: {}", config_file);
    const auto config_str = read_file(config_file);
    for (const auto& line : split(config_str, "\n")) {
      logger()->trace("line: {}", line);
      auto kv = split(line, "=");
      if (kv.size() != 2) {
        logger()->warn("Ignoring invalid config line: {}", line);
      } else {
        trim(kv[0]);
        trim(kv[1]);
        update_value(kv[0], kv[1]);
      }
    }
    return true;
  }
  return false;
}

void FileConfig::update_value(const std::string& key,
                              const std::string& value) {
  if (settings_map<BoolSetting>.contains(key)) {
    const auto parsed = parse_bool(value);
    if (!parsed) {
      logger()->warn("{} = {} could not parsed as a boolean", key, value);
    } else {
      logger()->debug("{} set to {}", key, *parsed);
      m_bool_settings[settings_map<BoolSetting>.at(key)] = *parsed;
    }
  } else if (settings_map<IntSetting>.contains(key)) {
    const auto parsed = [&value]() -> std::optional<int> {
      try {
        return {std::stoi(value)};
      } catch (...) {
        return {};
      }
    }();
    if (!parsed) {
      logger()->warn("{} = {} could not parsed as an integer", key, value);
    } else {
      logger()->debug("{} set to {}", key, *parsed);
      m_int_settings[settings_map<IntSetting>.at(key)] = *parsed;
    }
  } else if (settings_map<StringSetting>.contains(key)) {
    m_string_settings[settings_map<StringSetting>.at(key)] = value;
    logger()->debug("{} set to {}", key, value);
  } else {
    logger()->warn("Unknown key '{}' in config file ignored", key);
  }
}

SingletonDirectoryFileConfig::SingletonDirectoryFileConfig() : FileConfig("") {
  // Read and apply config from disk
  for (const auto& config_dir : config_dirs()) {
    const auto config_file = config_dir / "zit" / "config.ini";
    if (try_file(config_file)) {
      break;
    }
  }
}

}  // namespace zit
