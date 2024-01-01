// #define ASIO_ENABLE_BUFFER_DEBUGGING
// #define ASIO_ENABLE_HANDLER_TRACKING

#include <fmt/core.h>
#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/use_awaitable.hpp>
#include <csignal>
#include <exception>
#include <memory>
#include <vector>
#include "common.hpp"  // NOLINT(misc-include-cleaner)
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

    zit::logger()->debug("[{}] resolved {}", m_id, results.begin()->endpoint());

    auto result = *results.begin();
    co_await m_socket.async_connect(result, asio::use_awaitable);

    zit::logger()->info("[{}] {} connected to server {}", m_id,
                        m_socket.local_endpoint(), result.endpoint());

    co_await m_socket.async_write_some(
        asio::buffer(fmt::format("Hello {}\n", m_id)), asio::use_awaitable);

    zit::logger()->info("[{}] Sent hello from {}", m_id,
                        m_socket.local_endpoint());
  }

 private:
  // This is supposedly fixed in newer versions of clang-tidy (but not yet in
  // the one I use) See https://github.com/llvm/llvm-project/issues/47384
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static unsigned m_counter;
  asio::ip::tcp::resolver m_resolver;
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

    zit::logger()->info("Starting client. Press Ctrl-C to stop.");

    std::vector<std::unique_ptr<Connection>> connections;
    for (int i = 0; i < 2; ++i) {
      zit::logger()->debug("Creating connection {}", i);
      connections.emplace_back(std::make_unique<Connection>(io_context));
      zit::logger()->debug("Spawning connection {}", i);
      co_spawn(io_context, connections.back()->connect(), rethrow);
      zit::logger()->debug("Spawned connection {}", i);
    }
    io_context.run();
    zit::logger()->info("Shutting down client");
  } catch (const std::exception& e) {
    zit::logger()->error("Exception: {}", e.what());
    return 1;
  }
  return 0;
}
