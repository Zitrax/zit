#pragma once

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
using pid_t = DWORD;
#else
#include <sys/types.h>
#endif

namespace zit {

/**
 * Launch background process which is stopped/killed by the destructor.
 */
class Process {
 public:
  /**
   * Mode of running the process.
   */
  enum class Mode {
    /** Run in background. Constructor checks that it's up and running */
    BACKGROUND,
    /** For now this just means the constructor check is not done */
    FOREGROUND
  };

  Process(const std::string& name,
          std::vector<std::string> argv,
          const char* cwd = nullptr,
          std::vector<std::string> stop_cmd = {},
          Mode background = Mode::BACKGROUND);

  // Since this should be created and deleted only once
  // forbid copying and assignment.
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  // But allow moving
  Process(Process&& rhs)
      : m_pid(rhs.m_pid),
        m_name(rhs.m_name),
        // Copy to avoid double stop cmd
        m_stop_cmd(rhs.m_stop_cmd),
        m_mode(rhs.m_mode),
        m_cwd(rhs.m_cwd) {
    rhs.m_pid = 0;  // Ensure we wont kill the moved from process
    rhs.m_stop_cmd.clear();
  }
  Process& operator=(Process&& rhs) {
    m_pid = rhs.m_pid;
    m_name = rhs.m_name;
    m_stop_cmd = std::move(rhs.m_stop_cmd);
    rhs.m_pid = 0;  // Ensure we wont kill the moved from process
    return *this;
  }

  /** The pid of the started process */
  [[nodiscard]] pid_t pid() const { return m_pid; }

  /**
   * Terminate the process. Try to be nice and send SIGTERM first, and SIGKILL
   * later if that did not help.
   */
  void terminate();

  /**
   * Wait for the process to exit.
   * @param timeout The max time to wait for the process to exit.
   * @return True if the process exited, false if the timeout was reached.
   */
  [[nodiscard]] bool wait_for_exit(std::chrono::milliseconds timeout);

  /**
   * Kill the process if not already dead.
   */
  ~Process() {
    try {
      terminate();
    } catch (std::exception& ex) {
      try {
        std::cout << "ERROR in ~Process: " << ex.what();
      } catch (...) {
      }
    }
  }

 private:
  pid_t m_pid;
  std::string m_name;
  std::vector<std::string> m_stop_cmd;
  Mode m_mode;
  const char* m_cwd;
};

}  // namespace zit
