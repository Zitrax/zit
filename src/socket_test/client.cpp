#include <bits/chrono.h>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/basic_resolver.hpp>
#include <asio/ip/tcp.hpp>
#include <csignal>
#include <exception>
#include <tuple>
#include "formatters.hpp"  // NOLINT(misc-include-cleaner)
#include "logger.hpp"

// Can't use cstring currently - see
// https://github.com/llvm/llvm-project/issues/76567
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <string.h>

// TODO: Share signal handling
namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool aborted = false;

// ctrl-c linux signal handler
void signal_handler(int signum) {
  if (signum == SIGINT) {
    zit::logger()->debug("Caught abort/ctrl-c");
    aborted = true;
  } else {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    zit::logger()->info("Got signal '{}'", strsignal(signum));
  }
}

// Register the signal handler
void register_signal_handler() {
  std::ignore = signal(SIGINT, signal_handler);
}

}  // namespace

class Connection {
 public:
  explicit Connection(asio::io_context& io_context)
      :  // m_io_context(io_context),
        m_resolver(io_context),
        m_socket(io_context) {}

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
  // asio::io_context& m_io_context;
  asio::ip::tcp::resolver m_resolver;
  asio::ip::tcp::socket m_socket;
};

int main() {
  using namespace std::chrono_literals;
  try {
    register_signal_handler();
    zit::logger()->info("Starting client. Press Ctrl-C to stop.");
    asio::io_context io_context;
    Connection connection(io_context);
    connection.connect();
    while (!aborted) {
      io_context.run_for(100ms);
    }
    zit::logger()->info("Shutting down client");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
