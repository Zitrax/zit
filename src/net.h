// -*- mode:c++; c-basic-offset : 2; - * -
#pragma once

#include <ostream>
#include <string>
#include <tuple>
#include <vector>

namespace zit {

using string_list = std::vector<std::string>;

/**
 * Simplified URL parsing that covers cases we are interested in
 */
class Url {
 public:
  /**
   * Create URL object from string
   *
   * @param binary if true the string length is expected to be exactly 6, the
   *   first 4 bytes is the ip and the two last bytes the port.
   */
  Url(const std::string& url, bool binary = false);

  Url& add_param(const std::string& param);

  auto scheme() const { return m_scheme; }
  auto host() const { return m_host; }
  auto path() const { return m_path; }
  auto port() const { return m_port; }
  auto params() const { return m_params; }

 private:
  std::string m_scheme = "";
  std::string m_host = "";
  std::string m_path = "";
  string_list m_params{};
  unsigned short m_port = 0;
};

inline std::ostream& operator<<(std::ostream& os, const zit::Url& url) {
  os << "Scheme: " << url.scheme() << "\n";
  os << "Host:   " << url.host() << "\n";
  os << "Port:   " << url.port() << "\n";
  os << "Path:   " << url.path() << "\n";
  if (!url.params().empty()) {
    os << "Params:\n";
    for (const auto& param : url.params()) {
      os << "  " << param << "\n";
    }
  }
  return os;
}

/**
 * Class for handling network requests.
 */
class Net {
 public:
  Net() = default;

  std::tuple<std::string, std::string> http_get(const std::string& server,
                                                const std::string& path = "/",
                                                uint16_t port = 80,
                                                string_list params = {});

  auto http_get(const Url& url) {
    return http_get(url.host(), url.path(), url.port(), url.params());
  }

  /**
   * URL encode string.
   */
  static std::string url_encode(const std::string& value);
};

}  // namespace zit
