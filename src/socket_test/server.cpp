#include <bits/chrono.h>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <csignal>
#include <exception>
#include <tuple>
#include <vector>
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
  using namespace std::chrono_literals;
  try {
    // TODO: Apparently there is a simpler way by instructing asio to handle
    // signals
    register_signal_handler();
    zit::logger()->info("Starting server. Press Ctrl-C to stop.");
    asio::io_context io_context;
    Connection connection(io_context);
    co_spawn(io_context, connection.listen(), asio::detached);
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
