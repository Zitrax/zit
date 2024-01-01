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
#include <cstddef>
#include <exception>
#include <istream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "common.hpp"  // NOLINT(misc-include-cleaner)
#include "logger.hpp"

class Connection : public socket_test::ID {
 public:
  explicit Connection(asio::io_context& io_context,
                      asio::ip::tcp::acceptor& acceptor)
      : m_acceptor(acceptor), m_socket(io_context) {}

  // Listen for incoming connections
  // Using asio start listening for incoming connections
  asio::awaitable<void> listen() {
    m_acceptor.listen();
    while (true) {
      zit::logger()->info("[{}] Listening for incoming connections on {}", id(),
                          m_acceptor.local_endpoint());

      // Await incoming connection
      co_await m_acceptor.async_accept(m_socket, asio::use_awaitable);

      // Handle incoming connection
      zit::logger()->info("[{}] Accepted connection from {}", id(),
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

      zit::logger()->info("[{}] Received message: '{}' from {}", id(), message,
                          m_socket.remote_endpoint());
      m_socket.close();
    }
  }

 private:
  asio::ip::tcp::acceptor& m_acceptor;
  asio::ip::tcp::socket m_socket;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
unsigned socket_test::ID::m_counter = 0;

int main(int argc, char* argv[]) {
  try {
    asio::io_context io_context;

    // Ensuring ctrl-c shuts down cleanly
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&io_context](auto, auto) { io_context.stop(); });

    zit::logger()->info("Starting server. Press Ctrl-C to stop.");

    // Create acceptor for all connections
    asio::ip::tcp::acceptor acceptor(
        io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 8080));

    const auto args = std::vector<std::string>{argv, std::next(argv, argc)};
    const size_t num_connections = argc == 2 ? std::stoul(args.at(1)) : 1UL;
    std::vector<std::unique_ptr<Connection>> connections;
    connections.reserve(num_connections);
    for (size_t i = 0; i < num_connections; i++) {
      connections.emplace_back(
          std::make_unique<Connection>(io_context, acceptor));

      // Note: asio::detached is often used in examples but it will ignore any
      //       exceptions thrown by the coroutine. Using my own completion
      //       handler here instead that just rethrows the exception instead.
      co_spawn(io_context, connections.back()->listen(), socket_test::rethrow);
    }
    io_context.run();
    zit::logger()->info("Shutting down server");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
