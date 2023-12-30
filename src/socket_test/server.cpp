
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/use_awaitable.hpp>
#include <csignal>
#include <exception>
#include <vector>
#include "formatters.hpp"  // NOLINT(misc-include-cleaner)
#include "logger.hpp"

class Connection {
 public:
  explicit Connection(asio::io_context& io_context)
      : m_io_context(io_context),
        m_acceptor(io_context,
                   asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 8080)) {}

  // Listen for incoming connections
  // Using asio start listening for incoming connections
  asio::awaitable<void> listen() {
    m_acceptor.listen();
    while (true) {
      zit::logger()->info("Listening for incoming connections on {}",
                          m_acceptor.local_endpoint());

      m_sockets.emplace_back(m_io_context);
      auto& socket = m_sockets.back();

      // Await incoming connection
      co_await m_acceptor.async_accept(socket, asio::use_awaitable);

      // Handle incoming connection
      zit::logger()->info("Accepted connection from {}",
                          socket.remote_endpoint());
    }
  }

 private:
  asio::io_context& m_io_context;
  asio::ip::tcp::acceptor m_acceptor;
  std::vector<asio::ip::tcp::socket> m_sockets;
};

int main() {
  try {
    asio::io_context io_context;

    // Ensuring ctrl-c shuts down cleanly
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&io_context](auto, auto) { io_context.stop(); });

    zit::logger()->info("Starting server. Press Ctrl-C to stop.");
    Connection connection(io_context);
    co_spawn(io_context, connection.listen(), asio::detached);
    io_context.run();
    zit::logger()->info("Shutting down server");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
