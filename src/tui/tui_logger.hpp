#include <logger.hpp>

namespace zit::tui {

/**
 * Get the main logger for TUI.
 */
inline auto zit_logger() {
  return zit::logger("zit", LogOutput::MEMORY);
}

/**
 * Get the TUI specific file writer logger.
 */
inline auto file_writer_logger() {
  return zit::logger("file_writer", LogOutput::MEMORY);
}

/**
 * Get the TUI specific logger.
 */
inline auto logger() {
  return zit::logger("tui", LogOutput::MEMORY);
}

}  // namespace zit::tui
