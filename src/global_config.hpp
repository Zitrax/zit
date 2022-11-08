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

enum class BoolSetting {
  /**
   * If this is true, Zit will initiate connections to peers from the tracker
   * even if we are just seeding. This works around a problem where some
   * torrent clients refuse to connect to localhost if both clients are on
   * localhost (as is the case for the current integration tests).
   */
  INITIATE_PEER_CONNECTIONS
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

 protected:
  // Default values for all settings
  std::map<BoolSetting, bool> m_bool_settings{
      {BoolSetting::INITIATE_PEER_CONNECTIONS, true}};
};

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

  std::shared_ptr<spdlog::logger> m_logger;
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
 * Specification.
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
  SingletonDirectoryFileConfig();
};

}  // namespace zit
