// -*- mode:c++; c-basic-offset : 2; -*-
#include "net.hpp"

#include <fmt/format.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <asio/buffer.hpp>
#include <asio/buffers_iterator.hpp>
#include <asio/completion_condition.hpp>
#include <asio/connect.hpp>
#include <asio/detail/socket_ops.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/io_service.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>
#include <asio/read_until.hpp>
#include <asio/ssl/context.hpp>
#include <asio/ssl/rfc2818_verification.hpp>
#include <asio/ssl/stream.hpp>
#include <asio/ssl/verify_context.hpp>
#include <asio/ssl/verify_mode.hpp>
#include <asio/steady_timer.hpp>
#include <asio/streambuf.hpp>
#include <asio/system_error.hpp>

#ifndef _MSC_VER
#include <bits/basic_string.h>
#endif  // !_MSC_VER
#if __clang__
#include <bits/chrono.h>
#endif  // __clang__
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "logger.hpp"
#include "string_utils.hpp"
#include "types.hpp"

using asio::detail::socket_ops::host_to_network_short;
using asio::ip::tcp;
using namespace std;

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
    for (const auto& marker : m_markers) {
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

namespace {

template <typename Sock>
std::tuple<std::string, std::string> request(Sock& sock, const Url& url) {
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

  constexpr std::array VALID_STATUSES = {Net::m_http_status_ok,
                                         Net::m_http_status_found,
                                         Net::m_http_status_moved};
  if (ranges::find(VALID_STATUSES, status_code) == VALID_STATUSES.end()) {
    throw runtime_error(fmt::format("{}: response returned with status code {}",
                                    url.str(), status_code));
  }

  // Read the response headers, which are terminated by a blank line.
  try {
    asio::read_until(sock, response, MatchMarkers({"\r\n\r\n", "\n\n"}));
  } catch (const std::system_error& err) {
    if (err.code().category().name() != "asio.misc"s ||
        err.code().value() != asio::error::misc_errors::eof) {
      throw;
    }
  }

  // Process the response headers.
  string encoding;
  string header;
  stringstream headers;
  while (getline(response_stream, header) && header != "\r" &&
         !header.empty()) {
    const auto lheader = to_lower(header);
    constexpr auto location = "location: ";
    if (lheader.starts_with(location)) {
      const Url loc(rtrim_copy(header.substr(strlen(location), string::npos)));
      logger()->debug("Redirecting to {}", loc.str());
      return Net::httpGet(loc);
    }
    constexpr auto tencoding = "transfer-encoding: ";
    if (lheader.starts_with(tencoding)) {
      encoding = rtrim_copy(header.substr(strlen(tencoding), string::npos));
      if (encoding != "chunked") {
        throw runtime_error(encoding +
                            " http transfer encoding currently not supported");
      }
    }
    headers << header << "\n";
  }

  logger()->trace("=====RESPONSE=====\n'{}'\n", headers.str());

  stringstream resp;
  asio::error_code error;

  auto check_error = [&error, &url] {
    // FIXME: For some reason https get "stream truncated" here. Might be
    // related to "SSL short read" discussed here:
    // https://github.com/boostorg/beast/issues/38. So far I am not sure what
    // the "proper" fix for this is.
    if (url.scheme() != "https") {
      if (error && error != asio::error::eof) {
        throw asio::system_error(error, url.str());
      }
    }
  };

  if (encoding == "chunked") {
    logger()->debug("chunked transfer encoding");
    while (true) {
      logger()->trace("  response.size()={}", response.size());
      string chunk_str_len;
      if (response.size() == 0) {
        logger()->trace("  Read next line");
        asio::read_until(sock, response, MatchMarkers({"\r\n\r\n", "\n\n"}));
        logger()->trace("  response.size()={}", response.size());
      }
      getline(response_stream, chunk_str_len);
      rtrim(chunk_str_len);
      logger()->trace("  chunk_len_str='{}'", chunk_str_len);
      auto chunk_len =
          numeric_cast<unsigned long>(std::stol(chunk_str_len, nullptr, 16));
      logger()->trace("  chunk len={}", chunk_len);
      if (chunk_len == 0) {
        break;
      }
      chunk_len += 2;  // To include \r\n
      if (response.size() < chunk_len) {
        const auto read_n = chunk_len - response.size();
        logger()->trace("  read_n={}", read_n);
        error.clear();
        asio::read(sock, response, asio::transfer_at_least(read_n), error);
        check_error();
      }
      std::copy_n(asio::buffers_begin(response.data()), chunk_len - 2,
                  std::ostream_iterator<char>(resp));
      response.consume(chunk_len);
    }
  } else {
    // Write whatever content we already have to output.
    if (response.size() > 0) {
      resp << &response;
    }

    // Read until EOF, writing data to output as we go.
    error.clear();
    while (asio::read(sock, response, asio::transfer_at_least(1), error)) {
      resp << &response;
    }

    check_error();
  }

  return {headers.str(), resp.str()};
}

#ifdef _WIN32

#include <wincrypt.h>

/**
 * On Windows we do not seem to have a ready certificate directory as on Linux.
 * However we can read the certificates from the windows store and add them to
 * the openssl store.
 */
void addWindowsCertificates(const SSL_CTX* ctx) {
  auto hStore = CertOpenSystemStoreW(NULL, L"ROOT");
  if (!hStore) {
    logger()->warn("Could not open certificate store");
    return;
  }

  X509_STORE* store = SSL_CTX_get_cert_store(ctx);
  PCCERT_CONTEXT pContext = nullptr;

  while (true) {
    pContext = CertEnumCertificatesInStore(hStore, pContext);
    if (!pContext) {
      break;
    }
    auto* x509 = d2i_X509(
        nullptr, const_cast<const unsigned char**>(&pContext->pbCertEncoded),
        pContext->cbCertEncoded);
    if (x509) {
      if (X509_STORE_add_cert(store, x509) != 1) {
        logger()->warn("Could not add certificate");
      }
      X509_free(x509);
    }
  }
  CertCloseStore(hStore, 0);
}

#endif  // _WIN32

// Based on example at
// https://www.boost.org/doc/libs/1_73_0/doc/html/boost_asio/overview/ssl.html
std::tuple<std::string, std::string> httpsGet(const Url& url,
                                              const std::string& bind_address) {
  using ssl_socket = ssl::stream<tcp::socket>;
  // Create a context that uses the default paths for
  // finding CA certificates.
  ssl::context ctx(ssl::context::tlsv12);

  if (!bind_address.empty() && bind_address != "127.0.0.1") {
    throw std::runtime_error("bind for ssl not yet supported");
  }

#ifdef _WIN32
  addWindowsCertificates(ctx.native_handle());
#endif  // _WIN32

  ctx.set_default_verify_paths();
  // In my case the default path is not aligned with the systems on Ubuntu
  // (/usr/local/ssl/certs). Thus adding the standard linux directory in
  // addition. This is likely due OpenSSL being built by Conan with that path.
  // Note: With a newer conan openssl build this was no longer necessary
  // ctx.add_verify_path("/etc/ssl/certs");

  // Open a socket and connect it to the remote host.
  asio::io_context io_context;
  ssl_socket sock(io_context, ctx);
  tcp::resolver resolver(io_context);
  const tcp::resolver::query query(url.host(), url.service());
  asio::connect(sock.lowest_layer(), resolver.resolve(query));
  sock.lowest_layer().set_option(tcp::no_delay(true));

  // Perform SSL handshake and verify the remote host's
  // certificate.
  sock.set_verify_mode(ssl::verify_peer);

  // Without this some servers fail with "handshake: sslv3 alert handshake
  // failure" These servers rely on SNI (Server Name Indication)
  if (!SSL_set_tlsext_host_name(sock.native_handle(), url.host().c_str())) {
    logger()->warn("Could not set host name for SNI");
  }

  sock.set_verify_callback(
      [&url](bool preverified, asio::ssl::verify_context& ctx_) {
        if (!preverified) {
          logger()->warn("SSL: {}",
                         X509_verify_cert_error_string(
                             X509_STORE_CTX_get_error(ctx_.native_handle())));

          // According to cppreference std::getenv is thread safe
          // NOLINTNEXTLINE(concurrency-mt-unsafe)
          const char* dir = std::getenv(X509_get_default_cert_dir_env());
          if (!dir) {
            dir = X509_get_default_cert_dir();
          }
          logger()->warn(
              "SSL: Default cert dir: '{}'. If the certificates are elsewhere "
              "you can point SSL_CERT_DIR there.",
              dir);
        }

        const ssl::rfc2818_verification rfc2818(url.host());
        return rfc2818(preverified, ctx_);
      });
  sock.handshake(ssl_socket::client);

  return request(sock, url);
}

}  // namespace

//
// TODO: Ensure we can make a http(s) request from the bind address
//

//
// Implementation based on the example at:
// https://www.boost.org/doc/libs/1_36_0/doc/html/boost_asio/example/http/client/sync_client.cpp
//
std::tuple<std::string, std::string> Net::httpGet(
    const Url& url,
    const std::string& bind_address) {
  if (url.scheme() == "https" || url.port().value_or(1) == 443) {
    return httpsGet(url, bind_address);
  }

  if (url.scheme() != "http") {
    throw std::runtime_error("httpGet called on non-http url: " + url.str());
  }

  asio::io_service io_service;
  tcp::resolver resolver(io_service);
  auto endpoints = resolver.resolve(url.host(), url.service());

  // Try each endpoint until we successfully establish a connection.
  // An endpoint might be IPv4 or IPv6 (currently hardcoded to v4)
  tcp::socket socket(io_service);
  try {
    if (!bind_address.empty()) {
      socket.open(asio::ip::tcp::v4());
      socket.bind(
          asio::ip::tcp::endpoint(asio::ip::make_address(bind_address), 0));
      logger()->debug("Http request from {}:{}",
                      socket.local_endpoint().address().to_string(),
                      socket.local_endpoint().port());
    }
  } catch (const std::exception&) {
    throw_with_nested(std::runtime_error(
        fmt::format("Could not bind to address: '{}'", bind_address)));
  }
  asio::error_code error = asio::error::host_not_found;
  asio::connect(socket, endpoints, error);
  if (error) {
    throw asio::system_error(error, url.str());
  }

  return request(socket, url);
}

bytes Net::udpRequest(const Url& url,
                      const bytes& data,
                      std::chrono::duration<unsigned> timeout) {
  logger()->trace("udpRequest to {} of {} bytes (timeout={})", url.str(),
                  data.size(), duration_cast<chrono::seconds>(timeout).count());

  if (url.scheme() != "udp") {
    throw std::runtime_error("udpGet called on non-udp url: " + url.str());
  }
  if (!url.port()) {
    throw std::runtime_error("udp url without port not supported: " +
                             url.str());
  }

  asio::io_service io_service;
  // Automatically assign a local port for receiving responses on
  asio::ip::udp::socket socket(io_service,
                               asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));

  // Setup listening for reply
  bytes reply;
  asio::ip::udp::endpoint sender_endpoint;
  logger()->trace("udp listening on: {}:{}",
                  socket.local_endpoint().address().to_string(),
                  socket.local_endpoint().port());
  socket.async_receive_from(
      asio::null_buffers(), sender_endpoint,
      [&](const asio::error_code& error,
          size_t /*not interesting since we use null_buffers */) {
        if (error == asio::error::operation_aborted) {
          logger()->debug("udp request aborted");
        } else if (error) {
          logger()->warn("udp request failed: {}", error.message());
        } else {
          const auto available = socket.available();
          reply.resize(available);
          socket.receive_from(asio::buffer(reply), sender_endpoint);
          logger()->debug("udp received {} bytes from {}:{}", available,
                          sender_endpoint.address().to_string(),
                          sender_endpoint.port());
          io_service.stop();
        }
      });

  // Send outgoing request
  asio::ip::udp::resolver resolver(io_service);
  const auto receiver_endpoint =
      *resolver.resolve(asio::ip::udp::v4(), url.host(), url.service()).begin();
  socket.send_to(asio::buffer(data), receiver_endpoint);

  io_service.run_for(timeout);

  if (reply.empty()) {
    logger()->warn("udp request got no reply");
  }

  return reply;
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

Url::Url(const string& url, Binary binary, Resolve resolve) {
  if (!binary.get()) {
    const regex ur("^(udp|https?)://([^:/]*)(?::(\\d+))?(.*?)$");
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

  if (resolve.get()) {
    this->resolve();
  }
}

void Url::resolve() {
  logger()->trace("Trying to resolve {}", str());

  // On docker we get addresses in the 172.17.0 range. The host on win/mac can't
  // directly connect to that range and have to use the exposed ports on the
  // host. Lets just try to translate it here.
  constexpr auto DOCKER_IP_PREFIX{"172.17."};
  if (m_host.starts_with(DOCKER_IP_PREFIX)) {
    logger()->debug("Translated docker address {} to localhost", m_host);
    m_host = "localhost";
    return;
  }

  asio::error_code err;
  asio::ip::make_address(m_host, err);
  if (err) {
    // Already resolved or invalid address - so don't bother
    logger()->trace("{} does not need resolving", str());
    return;
  }

  // Note this is just doing a best effort at not spending too much time
  // resolving. However as for example https://stackoverflow.com/q/6407273/11722
  // shows, canceling an ongoing resolve will not actually cancelling the low
  // level resolve, so it can still take up to 10s.

  try {
    asio::io_service io_service;
    asio::ip::tcp::resolver resolver(io_service);
    auto original_host = m_host;
    const auto endpoint = asio::ip::tcp::endpoint(
        asio::ip::address_v4::from_string(m_host), m_port.value_or(0));

    asio::steady_timer timer(io_service);
    timer.expires_from_now(100ms);

    resolver.async_resolve(
        endpoint, [&](const asio::error_code& error,
                      const asio::ip::tcp::resolver::results_type& results) {
          timer.cancel();
          if (!error) {
            m_host = results->host_name();
            logger()->debug("Resolved {} -> {}", original_host, m_host);
            io_service.stop();
          } else if (error == asio::error::operation_aborted) {
            logger()->trace("Resolve aborted");
          } else {
            logger()->debug("Could not resolve: {} ({})", str(),
                            error.message());
          }
        });

    timer.async_wait([&](const asio::error_code& error) {
      if (error != asio::error::operation_aborted) {
        logger()->trace("Resolver for {} aborted by timeout", str());
        resolver.cancel();
      }
    });

    io_service.run();
  } catch (const std::exception&) {
    std::throw_with_nested(std::runtime_error("Resolve error"));
  }
}

}  // namespace zit
