// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

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
class DefaultConfig {
 public:
  virtual ~DefaultConfig() = default;

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
 */
class FileConfig : public DefaultConfig {
 private:
  FileConfig();

  std::shared_ptr<spdlog::logger> m_logger;

  void update_value(const std::string& key, const std::string& value);

 public:
  ~FileConfig() override = default;
  FileConfig(const FileConfig&) = delete;
  FileConfig& operator=(const FileConfig&) = delete;

  static FileConfig& getInstance() {
    // Meyers singleton
    static FileConfig config;
    return config;
  }
};

}  // namespace zit
