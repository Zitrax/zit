#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
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
  void connect() {
    m_resolver.async_resolve(
        "127.0.0.1", "8080",
        [&](const asio::error_code& resolve_error,
            const asio::ip::tcp::resolver::results_type& results) {
          if (!resolve_error) {
            m_socket.async_connect(
                *results.begin(),
                [results](const asio::error_code& connect_error) {
                  if (!connect_error) {
                    zit::logger()->info("Connected to server {}",
                                        results.begin()->endpoint());
                  } else {
                    zit::logger()->error("Failed to connect to server: {}",
                                         connect_error.message());
                  }
                });
          } else {
            zit::logger()->error("Failed to resolve server: {}",
                                 resolve_error.message());
          }
        });
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
    connection.connect();
    io_context.run();
    zit::logger()->info("Shutting down client");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
