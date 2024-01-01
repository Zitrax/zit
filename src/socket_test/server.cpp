// #define ASIO_ENABLE_BUFFER_DEBUGGING
// #define ASIO_ENABLE_HANDLER_TRACKING

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/read_until.hpp>
#include <asio/signal_set.hpp>
#include <asio/streambuf.hpp>
#include <asio/use_awaitable.hpp>
#include <csignal>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "common.hpp"  // NOLINT(misc-include-cleaner)
#include "logger.hpp"

#include <iostream>

class Connection {
 public:
  explicit Connection(asio::io_context& io_context,
                      asio::ip::tcp::acceptor& acceptor)
      : m_acceptor(acceptor), m_socket(io_context) {}

  // Listen for incoming connections
  // Using asio start listening for incoming connections
  asio::awaitable<void> listen() {
    m_acceptor.listen();
    while (true) {
      zit::logger()->info("[{}] Listening for incoming connections on {}", m_id,
                          m_acceptor.local_endpoint());

      // Await incoming connection
      co_await m_acceptor.async_accept(m_socket, asio::use_awaitable);

      // Handle incoming connection
      zit::logger()->info("[{}] Accepted connection from {}", m_id,
                          m_socket.remote_endpoint());

      // Read incoming data
      asio::streambuf buffer;
      co_await asio::async_read_until(m_socket, buffer, '\n',
                                      asio::use_awaitable);

      // Extract message without newline
      std::istream is(&buffer);
      std::string message;
      std::getline(is, message);

      // Print message

      zit::logger()->info("[{}] Received message: '{}' from {}", m_id, message,
                          m_socket.remote_endpoint());
      m_socket.close();
    }
  }

 private:
  // This is supposedly fixed in newer versions of clang-tidy (but not yet in
  // the one I use) See https://github.com/llvm/llvm-project/issues/47384
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static unsigned m_counter;
  asio::ip::tcp::acceptor& m_acceptor;
  asio::ip::tcp::socket m_socket;
  unsigned m_id{m_counter++};
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
unsigned Connection::m_counter = 0;

int main() {
  try {
    asio::io_context io_context;

    // Ensuring ctrl-c shuts down cleanly
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&io_context](auto, auto) { io_context.stop(); });

    zit::logger()->info("Starting server. Press Ctrl-C to stop.");

    // Create acceptor for all connections
    asio::ip::tcp::acceptor acceptor(
        io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 8080));

    std::vector<std::unique_ptr<Connection>> connections;
    for (int i = 0; i < 2; i++) {
      connections.emplace_back(
          std::make_unique<Connection>(io_context, acceptor));
      co_spawn(io_context, connections.back()->listen(), rethrow);
    }
    io_context.run();
    zit::logger()->info("Shutting down server");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
