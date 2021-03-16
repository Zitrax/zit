// -*- mode:c++; c-basic-offset : 2; -*-
#include "net.h"

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <spdlog/spdlog.h>
#include <experimental/array>
#include <iomanip>
#include <iostream>
#include <regex>
#include <stdexcept>

#include "string_utils.h"
#include "types.h"

using asio::detail::socket_ops::host_to_network_short;
using asio::ip::tcp;
using namespace std;
using std::experimental::make_array;
namespace ssl = asio::ssl;

namespace zit {

/**
 * When using asio::read_until we need to match multiple markers to handle non
 * standard responses.
 */
class MatchMarkers {
 public:
  explicit MatchMarkers(vector<string> markers)
      : m_markers(std::move(markers)) {}

  using iterator = asio::buffers_iterator<asio::streambuf::const_buffers_type>;

  template <typename Iterator>
  std::pair<Iterator, bool> operator()(Iterator begin, Iterator end) const {
    int i = 0;
    for (const auto& marker : m_markers) {
      i++;
      auto m = std::search(begin, end, std::begin(marker), std::end(marker));
      if (m != end) {
        return std::make_pair(next(m, numeric_cast<long>(marker.size())), true);
      }
    }
    return std::make_pair(end, false);
  }

 private:
  vector<string> m_markers;
};
}  // namespace zit

// Need to explicitly mark this as a MatchCondition
template <>
struct asio::is_match_condition<zit::MatchMarkers> : public std::true_type {};

namespace zit {

Url& Url::add_param(const std::string& param) {
  m_params.push_back(param);
  return *this;
}

template <typename Sock>
static std::tuple<std::string, std::string> request(Sock& sock,
                                                    const Url& url) {
  // Form the request. We specify the "Connection: close" header so that the
  // server will close the socket after transmitting the response. This will
  // allow us to treat all data up until the EOF as the content.
  asio::streambuf request;
  ostream request_stream(&request);
  std::stringstream rpath;
  rpath << url.path();
  auto it = url.params().begin();
  if (it != url.params().end()) {
    rpath << "?" << *it++;
  }
  while (it != url.params().end()) {
    rpath << "&" << *it++;
  }
  request_stream << "GET " << rpath.str() << " HTTP/1.1\r\n";
  request_stream << "Host: " << url.host() << "\r\n";
  request_stream << "Accept: */*\r\n";
  request_stream << "Connection: close\r\n\r\n";

  // Send the request.
  asio::write(sock, request);

  // Read the response status line.
  asio::streambuf response;
  asio::read_until(sock, response, MatchMarkers({"\r\n", "\n"}));

  // Check that response is OK.
  std::istream response_stream(&response);
  std::string http_version;
  response_stream >> http_version;
  unsigned int status_code = 0;
  response_stream >> status_code;
  std::string status_message;
  std::getline(response_stream, status_message);
  if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
    throw runtime_error("invalid response");
  }

  constexpr auto VALID_STATUSES =
      make_array(Net::m_http_status_ok, Net::m_http_status_found,
                 Net::m_http_status_moved);
  if (find(VALID_STATUSES.begin(), VALID_STATUSES.end(), status_code) ==
      VALID_STATUSES.end()) {
    throw runtime_error(fmt::format("{}: response returned with status code {}",
                                    url.str(), status_code));
  }

  // Read the response headers, which are terminated by a blank line.
  asio::read_until(sock, response, MatchMarkers({"\r\n\r\n", "\n\n"}));

  // Process the response headers.
  string header;
  stringstream headers;
  while (getline(response_stream, header) && header != "\r" &&
         !header.empty()) {
    constexpr auto location = "Location: ";
    if (header.starts_with(location)) {
      Url loc(rtrim_copy(header.substr(strlen(location), string::npos)));
      spdlog::get("console")->debug("Redirecting to {}", loc.str());
      return Net::httpGet(loc);
    }
    constexpr auto tencoding = "Transfer-Encoding: ";
    if (header.starts_with(tencoding)) {
      std::string encoding =
          rtrim_copy(header.substr(strlen(tencoding), string::npos));
      if (encoding == "chunked") {
        throw runtime_error(
            "chunked http transfer encoding currently not supported");
      }
    }
    headers << header << "\n";
  }

  spdlog::get("console")->trace("=====RESPONSE=====\n'{}'\n", headers.str());

  stringstream resp;
  // Write whatever content we already have to output.
  if (response.size() > 0) {
    resp << &response;
  }

  // Read until EOF, writing data to output as we go.
  asio::error_code error;
  while (asio::read(sock, response, asio::transfer_at_least(1), error)) {
    resp << &response;
  }

  // FIXME: For some reason https get "stream truncated" here. Might be
  // related to "SSL short read" discussed here:
  // https://github.com/boostorg/beast/issues/38. So far I am not sure what
  // the "proper" fix for this is.
  if (url.scheme() != "https") {
    if (error != asio::error::eof) {
      throw asio::system_error(error, url.str());
    }
  }

  return {headers.str(), resp.str()};
}

// Based on example at
// https://www.boost.org/doc/libs/1_73_0/doc/html/boost_asio/overview/ssl.html
static std::tuple<std::string, std::string> httpsGet(const Url& url) {
  using ssl_socket = ssl::stream<tcp::socket>;
  // Create a context that uses the default paths for
  // finding CA certificates.
  ssl::context ctx(ssl::context::sslv23);

  ctx.set_default_verify_paths();
  // In my case the default path is not aligned with the systems on Ubuntu
  // (/usr/local/ssl/certs). Thus adding the standard linux directory in
  // addition. This is likely due OpenSSL being built by Conan with that path.
  ctx.add_verify_path("/etc/ssl/certs");

  // Open a socket and connect it to the remote host.
  asio::io_context io_context;
  ssl_socket sock(io_context, ctx);
  tcp::resolver resolver(io_context);
  tcp::resolver::query query(url.host(), url.service());
  asio::connect(sock.lowest_layer(), resolver.resolve(query));
  sock.lowest_layer().set_option(tcp::no_delay(true));

  // Perform SSL handshake and verify the remote host's
  // certificate.
  sock.set_verify_mode(ssl::verify_peer);
  sock.set_verify_callback(
      [&url](bool preverified, asio::ssl::verify_context& ctx_) {
        if (!preverified) {
          spdlog::get("console")->warn(
              "SSL: {}", X509_verify_cert_error_string(
                             X509_STORE_CTX_get_error(ctx_.native_handle())));

          const char* dir = getenv(X509_get_default_cert_dir_env());
          if (!dir) {
            dir = X509_get_default_cert_dir();
          }
          spdlog::get("console")->warn(
              "SSL: Default cert dir: '{}'. If the certificates are elsewhere "
              "you can point SSL_CERT_DIR there.",
              dir);
        }

        ssl::rfc2818_verification rfc2818(url.host());
        return rfc2818(preverified, ctx_);
      });
  sock.handshake(ssl_socket::client);

  return request(sock, url);
}  // namespace zit

//
// Implementation based on the example at:
// https://www.boost.org/doc/libs/1_36_0/doc/html/boost_asio/example/http/client/sync_client.cpp
//
std::tuple<std::string, std::string> Net::httpGet(const Url& url) {
  if (url.scheme() == "https" || url.port().value_or(1) == 443) {
    return httpsGet(url);
  }

  asio::io_service io_service;
  tcp::resolver resolver(io_service);
  auto endpoints = resolver.resolve(url.host(), url.service());

  // Try each endpoint until we successfully establish a connection.
  // An endpoint might be IPv4 or IPv6
  tcp::socket socket(io_service);
  asio::error_code error = asio::error::host_not_found;
  asio::connect(socket, endpoints, error);
  if (error) {
    throw asio::system_error(error, url.str());
  }

  return request(socket, url);
}

// Based on https://stackoverflow.com/a/17708801/11722
string Net::urlEncode(const string& value) {
  ostringstream escaped;
  escaped.fill('0');
  escaped << hex;

  for (const auto VAL : value) {
    // Windows need C to be unsigned or it will assert. This extra cast
    // was measured in quickbench to only have 0.1% impact.
    const auto C = static_cast<unsigned char>(VAL);
    // Keep alphanumeric and other accepted characters intact
    if (isalnum(C) || C == '-' || C == '_' || C == '.' || C == '~') {
      escaped << C;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << uppercase;
    escaped << '%' << setw(2) << int(static_cast<byte>(C));
    escaped << nouppercase;
  }

  return escaped.str();
}

Url::Url(const string& url, bool binary) {
  if (!binary) {
    regex ur("^(https?)://([^:/]*)(?::(\\d+))?(.*?)$");
    smatch match;
    if (!regex_match(url, match, ur) || match.size() != 5) {
      throw runtime_error("Invalid URL: '" + url + "'");
    }
    m_scheme = match.str(1);
    m_host = match.str(2);
    if (!match.str(3).empty()) {
      m_port = numeric_cast<uint16_t>(stoi(match.str(3)));
    } else {
      m_port = std::nullopt;
    }
    m_path = match.str(4);
    if (m_path.empty()) {
      m_path = "/";
    }
  } else {
    if (url.length() != 6) {
      throw runtime_error("Invalid binary URL length " +
                          to_string(url.length()));
    }
    stringstream ss;
    ss << to_string(uint8_t(url[0])) << "." << to_string(uint8_t(url[1])) << "."
       << to_string(uint8_t(url[2])) << "." << to_string(uint8_t(url[3]));
    m_host = ss.str();
    m_port = host_to_network_short(static_cast<uint16_t>(
        static_cast<uint8_t>(url[4]) << 0 | static_cast<uint8_t>(url[5]) << 8));
    m_scheme = "http";
  }
}

}  // namespace zit
