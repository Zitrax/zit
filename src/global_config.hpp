// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <variant>

namespace spdlog {
class logger;
}

namespace zit {

/**
 * Boolean settings. In the config file they can be read as true/false or 1/0
 */
enum class BoolSetting {
  /**
   * If this is true, Zit will initiate connections to peers from the tracker
   * even if we are just seeding. This works around a problem where some
   * torrent clients refuse to connect to localhost if both clients are on
   * localhost (as is the case for the current integration tests).
   */
  INITIATE_PEER_CONNECTIONS,
  /**
   * Spend extra time on resolving URLs
   */
  RESOLVE_URLS
};

/**
 * Settings taking an integer. An invalid integer for the setting is a runtime
 * error.
 */
enum class IntSetting {
  /**
   * Port on which the Zit client is listening to incoming peer connections.
   */
  LISTENING_PORT,

  /**
   * Port on which the Zit client is making outgoing peer connections.
   */
  CONNECTION_PORT
};

/**
 * Provides a config interface along with default values for all settings
 */
class Config {
 public:
  virtual ~Config() = default;

  /** Get the value of a bool setting */
  [[nodiscard]] virtual bool get(BoolSetting setting) const {
    return m_bool_settings.at(setting);
  }

  /** Get the value of an int setting */
  [[nodiscard]] virtual int get(IntSetting setting) const {
    return m_int_settings.at(setting);
  }

 protected:
  // Default values for all settings
  std::map<BoolSetting, bool> m_bool_settings{
      {BoolSetting::INITIATE_PEER_CONNECTIONS, false},
      {BoolSetting::RESOLVE_URLS, true}};

  std::map<IntSetting, int> m_int_settings{
      {IntSetting::LISTENING_PORT, 20001},
      {IntSetting::CONNECTION_PORT, 20000}};
};

std::ostream& operator<<(std::ostream& os, const Config& config);

/**
 * Reads config from disk on the format:
 *  KEY=VAL
 *
 * At the moment there is no support for escaping '='
 *
 * This class is mostly meant for testing such that we can lock in on one
 * specific file.
 */
class FileConfig : public Config {
 public:
  ~FileConfig() override = default;
  FileConfig(const FileConfig&) = delete;
  FileConfig& operator=(const FileConfig&) = delete;

  explicit FileConfig(std::filesystem::path config_file);

 protected:
  bool try_file(const std::filesystem::path& config_file);
  void update_value(const std::string& key, const std::string& value);

  std::filesystem::path m_config_file;
};

/**
 * Reads config from disk on the format:
 *  KEY=VAL
 *
 * At the moment there is no support for escaping '='
 *
 * This is the main config for Zit. It will look for the config file
 * zit/.zit in all the default locations based on XDG Base Directory
 * Specification and finally fall back on the home directory.
 *
 * An example config with all the current default options:
 *
 * initiate_peer_connections = false
 * listening_port = 20001
 * connection_port = 20000
 */
class SingletonDirectoryFileConfig : public FileConfig {
 public:
  ~SingletonDirectoryFileConfig() override = default;
  SingletonDirectoryFileConfig(const SingletonDirectoryFileConfig&) = delete;
  SingletonDirectoryFileConfig& operator=(const SingletonDirectoryFileConfig&) =
      delete;

  static SingletonDirectoryFileConfig& getInstance() {
    // Meyers singleton
    static SingletonDirectoryFileConfig config;
    return config;
  }

 private:
  // Can't delete it, we have an implementation
  // NOLINTNEXTLINE(modernize-use-equals-delete,hicpp-use-equals-delete)
  SingletonDirectoryFileConfig();
};

}  // namespace zit
