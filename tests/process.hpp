#pragma once

#ifdef __linux__

#include <iostream>
#include <string>
#include <vector>

namespace zit {

/**
 * Launch background process which is stopped/killed by the destructor.
 *
 * FIXME: Implement support for Windows. Currently linux only.
 */
class Process {
 public:
  Process(const std::string& name,
          std::vector<const char*> argv,
          const char* cwd = nullptr);

  // Since this should be created and deleted only once
  // forbid copying and assignment.
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;
  // But allow moving
  Process(Process&& rhs) : m_pid(rhs.m_pid), m_name(rhs.m_name) {
    rhs.m_pid = 0;  // Ensure we wont kill the moved from process
  }

  /**
   * Terminate the process. Try to be nice and send SIGTERM first, and SIGKILL
   * later if that did not help.
   */
  void terminate();

  /**
   * Kill the process if not already dead.
   */
  ~Process() {
    try {
      terminate();
    } catch (std::exception& ex) {
      std::cout << "ERROR in ~Process: " << ex.what();
    }
  }

 private:
  pid_t m_pid;
  std::string m_name;
};

}  // namespace zit

#endif  // __linux__
