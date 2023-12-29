#include <bits/chrono.h>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
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
      : m_io_context(io_context),
        m_acceptor(m_io_context,
                   asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 8080)) {}

  // Listen for incoming connections
  // Using asio start listening for incoming connections
  void listen() {
    zit::logger()->info("Listening for incoming connections on {}",
                        m_acceptor.local_endpoint());
    m_acceptor.listen();
    asio::ip::tcp::socket socket(m_io_context);

    m_acceptor.async_accept(socket, [&](const asio::error_code& error) {
      if (error) {
        zit::logger()->error("Error accepting connection: {}", error.message());
        return;
      }
      zit::logger()->info("Accepted connection from {}",
                          socket.remote_endpoint());
      //  Handle the connection
      //  ...

      // TODO: Might not be so smart to call it from within the callback
      //       Maybe queue it up instead?
      listen();
    });
  }

 private:
  asio::io_context& m_io_context;
  asio::ip::tcp::acceptor m_acceptor;
};

int main() {
  using namespace std::chrono_literals;
  try {
    register_signal_handler();
    zit::logger()->info("Starting server. Press Ctrl-C to stop.");
    asio::io_context io_context;
    Connection connection(io_context);
    connection.listen();
    while (!aborted) {
      io_context.run_for(100ms);
    }
    zit::logger()->info("Shutting down server");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
