// -*- mode:c++; c-basic-offset : 2; -*-
#include "net.h"

#include <asio.hpp>
#include <asio/ssl.hpp>

#include <spdlog/spdlog.h>
#include <experimental/array>
#include <iomanip>
#include <iostream>
#include <regex>

#include "string_utils.h"
#include "types.h"

using asio::detail::socket_ops::host_to_network_short;
using asio::ip::tcp;
using namespace std;
using std::experimental::make_array;
namespace ssl = asio::ssl;

namespace zit {

Url& Url::add_param(const std::string& param) {
  m_params.push_back(param);
  return *this;
}

template <typename Sock>
static std::tuple<std::string, std::string> request(Sock& sock,
                                                    const string& server,
                                                    const string& path,
                                                    const string& service,
                                                    const string_list& params) {
  // Form the request. We specify the "Connection: close" header so that the
  // server will close the socket after transmitting the response. This will
  // allow us to treat all data up until the EOF as the content.
  asio::streambuf request;
  ostream request_stream(&request);
  std::stringstream rpath;
  rpath << path;
  auto it = params.begin();
  if (it != params.end()) {
    rpath << "?" << *it++;
  }
  while (it != params.end()) {
    rpath << "&" << *it++;
  }
  request_stream << "GET " << rpath.str() << " HTTP/1.1\r\n";
  request_stream << "Host: " << server << "\r\n";
  request_stream << "Accept: */*\r\n";
  request_stream << "Connection: close\r\n\r\n";

  // Send the request.
  asio::write(sock, request);

  // Read the response status line.
  asio::streambuf response;
  asio::read_until(sock, response, "\r\n");

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
      make_array(Net::m_http_status_ok, Net::m_http_status_found);
  if (find(VALID_STATUSES.begin(), VALID_STATUSES.end(), status_code) ==
      VALID_STATUSES.end()) {
    throw runtime_error("response returned with status code " +
                        to_string(status_code));
  }

  // Read the response headers, which are terminated by a blank line.
  asio::read_until(sock, response, "\r\n\r\n");

  // Process the response headers.
  string header;
  stringstream headers;
  while (getline(response_stream, header) && header != "\r") {
    if (header.starts_with("Location: ")) {
      Url loc(rtrim_copy(header.substr(10, string::npos)));
      spdlog::get("console")->debug("Redirecting to {}", loc.str());
      return Net::httpGet(loc);
    }
    headers << header << "\n";
  }

  spdlog::get("console")->trace("=====RESPONSE=====\n{}\n", headers.str());

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

  // FIXME: For some reason https get "stream truncated" here. Might be related
  // to "SSL short read" discussed here:
  // https://github.com/boostorg/beast/issues/38. So far I am not sure what the
  // "proper" fix for this is.
  if (service != "https") {
    if (error != asio::error::eof) {
      throw asio::system_error(error);
    }
  }

  return {headers.str(), resp.str()};
}

// Based on example at
// https://www.boost.org/doc/libs/1_73_0/doc/html/boost_asio/overview/ssl.html
static std::tuple<std::string, std::string> httpsGet(
    const std::string& server,
    const std::string& path,
    const string& service,
    const string_list& params) {
  using ssl_socket = ssl::stream<tcp::socket>;
  // Create a context that uses the default paths for
  // finding CA certificates.
  ssl::context ctx(ssl::context::sslv23);

  // FIXME: Find the proper path, on my current system this will look in
  // "/usr/local/ssl/certs" while the correct location is "/etc/ssl/certs".
  ctx.set_default_verify_paths();

  // Open a socket and connect it to the remote host.
  asio::io_context io_context;
  ssl_socket sock(io_context, ctx);
  tcp::resolver resolver(io_context);
  tcp::resolver::query query(server, service);
  asio::connect(sock.lowest_layer(), resolver.resolve(query));
  sock.lowest_layer().set_option(tcp::no_delay(true));

  // Perform SSL handshake and verify the remote host's
  // certificate.
  sock.set_verify_mode(ssl::verify_peer);
  sock.set_verify_callback(
      [&server](bool preverified, asio::ssl::verify_context& ctx_) {
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

        ssl::rfc2818_verification rfc2818(server);
        return rfc2818(preverified, ctx_);
      });
  sock.handshake(ssl_socket::client);

  return request(sock, server, path, service, params);
}  // namespace zit

//
// Implementation based on the example at:
// https://www.boost.org/doc/libs/1_36_0/doc/html/boost_asio/example/http/client/sync_client.cpp
//
std::tuple<std::string, std::string> Net::httpGet(const string& server,
                                                  const string& path,
                                                  const string& service,
                                                  const string_list& params) {
  if (service == "https") {
    return httpsGet(server, path, service, params);
  }

  asio::io_service io_service;
  tcp::resolver resolver(io_service);
  auto endpoints = resolver.resolve(server, service);

  // Try each endpoint until we successfully establish a connection.
  // An endpoint might be IPv4 or IPv6
  tcp::socket socket(io_service);
  asio::error_code error = asio::error::host_not_found;
  asio::connect(socket, endpoints, error);
  if (error) {
    throw asio::system_error(error);
  }

  return request(socket, server, path, service, params);
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
}  // namespace zit

}  // namespace zit
