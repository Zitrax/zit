#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/use_awaitable.hpp>
#include <csignal>
#include <exception>
#include "formatters.hpp"  // NOLINT(misc-include-cleaner)
#include "logger.hpp"

class Connection {
 public:
  explicit Connection(asio::io_context& io_context)
      : m_resolver(io_context), m_socket(io_context) {}

  /**
   * Connect to server
   */
  asio::awaitable<void> connect() {
    // Unsure why clang-tidy complains here
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    auto results = co_await m_resolver.async_resolve("127.0.0.1", "8080",
                                                     asio::use_awaitable);

    // FIXME: Resolve failure
    // zit::logger()->error("Failed to resolve server: {}",
    //                             resolve_error.message());

    co_await m_socket.async_connect(*results.begin(), asio::use_awaitable);

    zit::logger()->info("Connected to server {}", results.begin()->endpoint());

    // zit::logger()->error("Failed to connect to server: {}",
    //                      connect_error.message());
  }

 private:
  asio::ip::tcp::resolver m_resolver;
  asio::ip::tcp::socket m_socket;
};

int main() {
  try {
    asio::io_context io_context;

    // Ensuring ctrl-c shuts down cleanly
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&io_context](auto, auto) { io_context.stop(); });

    zit::logger()->info("Starting client. Press Ctrl-C to stop.");
    Connection connection(io_context);
    co_spawn(io_context, connection.connect(), asio::detached);
    io_context.run();
    zit::logger()->info("Shutting down client");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
