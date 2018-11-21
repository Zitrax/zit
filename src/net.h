// -*- mode:c++; c-basic-offset : 2; - * -
#pragma once

#include <ostream>
#include <string>

namespace zit {

class Net {
 public:
  Net() = default;

  // TODO: url param and split/parse internally
  std::tuple<std::string, std::string> http_get(const std::string& server,
                                                const std::string& path = "/");
};

/**
 * Simplified URL parsing that covers cases we are interested in
 */
class Url {
 public:
  Url(const std::string& url);

  auto scheme() const { return m_scheme; }
  auto host() const { return m_host; }
  auto path() const { return m_path; }
  auto port() const { return m_port; }

 private:
  std::string m_scheme = "";
  std::string m_host = "";
  std::string m_path = "";
  unsigned short m_port = 0;
};

inline std::ostream& operator<<(std::ostream& os, const zit::Url& url) {
  os << "Scheme: " << url.scheme() << "\n";
  os << "Host:   " << url.host() << "\n";
  os << "Path:   " << url.path() << "\n";
  os << "Port:   " << url.port() << "\n";
  return os;
}

}  // namespace zit
