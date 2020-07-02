// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <optional>
#include <ostream>
#include <sstream>
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
  explicit Url(const std::string& url, bool binary = false);

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

  [[nodiscard]] auto scheme() const { return m_scheme; }
  [[nodiscard]] auto host() const { return m_host; }
  [[nodiscard]] auto path() const { return m_path; }
  [[nodiscard]] auto port() const { return m_port; }
  [[nodiscard]] auto params() const { return m_params; }
  [[nodiscard]] auto service() const {
    return m_port ? std::to_string(*m_port) : m_scheme;
  }

  /**
   * In an URL the authority is the "[userinfo@]host[:port]" part.
   *
   * At the moment the URL class does not include the userinfo.
   */
  [[nodiscard]] auto authority() const {
    return m_host + (m_port ? (":" + std::to_string(*m_port)) : "");
  }

  /** Url represented as a full string */
  [[nodiscard]] auto str() const {
    std::stringstream ss;
    ss << scheme() << "://" << authority() << path();
    if (!params().empty()) {
      bool first = true;
      for (const auto& param : params()) {
        ss << (first ? "?"s : "&"s) << param;
        first = false;
      }
    }
    return ss.str();
  }

 private:
  std::string m_scheme = "";
  std::string m_host = "";
  std::string m_path = "";
  string_list m_params{};
  std::optional<uint16_t> m_port{};
};

inline std::ostream& operator<<(std::ostream& os, const zit::Url& url) {
  const auto port = url.port() ? std::to_string(*url.port()) : "<not set>";
  os << "Scheme:        " << url.scheme() << "\n";
  os << "Host:          " << url.host() << "\n";
  os << "Port:          " << port << "\n";
  os << "Path:          " << url.path() << "\n";
  if (!url.params().empty()) {
    os << "Params:\n";
    for (const auto& param : url.params()) {
      os << "  " << param << "\n";
    }
  }
  os << "Full URL:      " << url.str() << "\n";
  return os;
}

/**
 * Class for handling network requests.
 */
class Net {
 public:
  constexpr static auto m_http_status_ok = 200;
  constexpr static auto m_http_status_found = 302;

  Net() = default;

  static std::tuple<std::string, std::string> httpGet(
      const std::string& server,
      const std::string& path = "/",
      const std::string& service = "http",
      const string_list& params = {});

  static auto httpGet(const Url& url) {
    return Net::httpGet(url.host(), url.path(), url.service(), url.params());
  }

  /**
   * URL encode string.
   */
  static std::string urlEncode(const std::string& value);

  /**
   * URL encode array.
   */
  template <std::size_t SIZE>
  static std::string urlEncode(const std::array<char, SIZE>& value) {
    return urlEncode(std::string(value.data(), value.size()));
  }
};

}  // namespace zit
