// -*- mode:c++; c-basic-offset : 2; - * -
#include "net.h"

#include <asio.hpp>

#include <iostream>
#include <regex>

using asio::ip::tcp;
using namespace std;

namespace zit {

//
// Implementation based on the example at:
// https://www.boost.org/doc/libs/1_36_0/doc/html/boost_asio/example/http/client/sync_client.cpp
//
std::tuple<std::string, std::string> Net::http_get(const string& server,
                                                   const string& path,
                                                   uint16_t port) {
  asio::io_service io_service;
  tcp::resolver resolver(io_service);
  tcp::resolver::query query(server, to_string(port));
  tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
  tcp::resolver::iterator end;

  // Try each endpoint until we successfully establish a connection.
  // An endpoint might be IPv4 or IPv6
  tcp::socket socket(io_service);
  asio::error_code error = asio::error::host_not_found;
  while (error && endpoint_iterator != end) {
    tcp::endpoint endpoint = *endpoint_iterator++;
    cerr << "Trying: " << endpoint << std::endl;
    socket.close();
    socket.connect(endpoint, error);
  }
  if (error) {
    throw asio::system_error(error);
  }

  // Form the request. We specify the "Connection: close" header so that the
  // server will close the socket after transmitting the response. This will
  // allow us to treat all data up until the EOF as the content.
  asio::streambuf request;
  ostream request_stream(&request);
  request_stream << "GET " << path << " HTTP/1.0\r\n";
  request_stream << "Host: " << server << "\r\n";
  request_stream << "Accept: */*\r\n";
  request_stream << "Connection: close\r\n\r\n";

  // Send the request.
  asio::write(socket, request);

  // Read the response status line.
  asio::streambuf response;
  asio::read_until(socket, response, "\r\n");

  // Check that response is OK.
  std::istream response_stream(&response);
  std::string http_version;
  response_stream >> http_version;
  unsigned int status_code;
  response_stream >> status_code;
  std::string status_message;
  std::getline(response_stream, status_message);
  if (!response_stream || http_version.substr(0, 5) != "HTTP/") {
    throw runtime_error("invalid response");
  }
  if (status_code != 200) {
    throw runtime_error("response returned with status code " +
                        to_string(status_code));
  }

  // Read the response headers, which are terminated by a blank line.
  asio::read_until(socket, response, "\r\n\r\n");

  // Process the response headers.
  string header;
  stringstream headers;
  while (getline(response_stream, header) && header != "\r") {
    headers << header << "\n";
  }

  stringstream resp;
  // Write whatever content we already have to output.
  if (response.size() > 0) {
    resp << &response;
  }

  // Read until EOF, writing data to output as we go.
  while (asio::read(socket, response, asio::transfer_at_least(1), error)) {
    resp << &response;
  }
  if (error != asio::error::eof) {
    throw asio::system_error(error);
  }

  return {headers.str(), resp.str()};
}

Url::Url(const string& url) {
  regex ur("^(https?)://([^:/]*)(?::(\\d+))?(.*?)$");
  cmatch match;
  // FIXME: Why the need for .c_str()?
  if (!regex_match(url.c_str(), match, ur) || match.size() != 5) {
    throw runtime_error("Invalid URL: " + url);
  }
  m_scheme = match[1];
  m_host = match[2];
  m_port = stoi(match[3]);
  m_path = match[4];
}

}  // namespace zit
