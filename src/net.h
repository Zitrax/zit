// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <ostream>
#include <string>
#include <tuple>
#include <vector>

namespace zit {

using string_list = std::vector<std::string>;
using namespace std::string_literals;

/**
 * Simplified URL parsing that covers cases we are interested in
 */
class Url {
 public:
  /**
   * Create Url object from string
   *
   * @param url string on the form http(s)://host:port/path if binary is false,
   *   otherwise see binary format doc.
   *
   * @param binary if true the string length is expected to be exactly 6, the
   *   first 4 bytes is the ip and the two last bytes the port.
   */
  Url(const std::string& url, bool binary = false);

  /**
   * Create Url object
   *
   * @param scheme (e.g. http or https)
   * @param host (e.g. localhost or google.com)
   * @param port (e.g. 80)
   * @param path (e.g. /index.html)
   */
  Url(std::string scheme,
      std::string host,
      unsigned short port,
      std::string path = "")
      : m_scheme(std::move(scheme)),
        m_host(std::move(host)),
        m_path(std::move(path)),
        m_port(port) {}

  /**
   * Add http query params to existing Url object.
   */
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
  os << "Scheme:        " << url.scheme() << "\n";
  os << "Host:          " << url.host() << "\n";
  os << "Port:          " << url.port() << "\n";
  os << "Path:          " << url.path() << "\n";
  if (!url.params().empty()) {
    os << "Params:\n";
    for (const auto& param : url.params()) {
      os << "  " << param << "\n";
    }
  }
  os << "Full URL:      " << url.scheme() << "://" << url.host() << ":"
     << url.port() << url.path();
  if (!url.params().empty()) {
    bool first = true;
    for (const auto& param : url.params()) {
      os << (first ? "?"s : "&"s) << param;
      first = false;
    }
  }
  os << "\n";
  return os;
}

/**
 * Class for handling network requests.
 */
class Net {
 public:
  constexpr static auto DEFAULT_HTTP_PORT = 80;
  constexpr static auto HTTP_STATUS_OK = 200;

  Net() = default;

  std::tuple<std::string, std::string> http_get(
      const std::string& server,
      const std::string& path = "/",
      uint16_t port = DEFAULT_HTTP_PORT,
      string_list params = {});

  auto http_get(const Url& url) {
    return http_get(url.host(), url.path(), url.port(), url.params());
  }

  /**
   * URL encode string.
   */
  static std::string url_encode(const std::string& value);

  /**
   * URL encode array.
   */
  template <std::size_t SIZE>
  static std::string url_encode(const std::array<char, SIZE>& value) {
    return url_encode(std::string(value.data(), value.size()));
  };
};

}  // namespace zit
