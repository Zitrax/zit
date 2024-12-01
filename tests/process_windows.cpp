#include "process.hpp"

#include <windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "logger.hpp"

namespace zit {

using namespace std;

Process::Process(const string& name,
                 vector<const char*> argv,
                 const char* cwd,
                 vector<const char*> stop_cmd)
    : m_pid(0), m_name(name), m_stop_cmd(move(stop_cmd)) {
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  string commandLine;
  for (const auto& arg : argv) {
    commandLine += string(arg) + " ";
  }

  if (!CreateProcess(nullptr, const_cast<char*>(commandLine.c_str()), nullptr,
                     nullptr, FALSE, 0, nullptr, cwd, &si, &pi)) {
    throw runtime_error("Failed to create process for "s + argv[0]);
  }

  m_pid = pi.dwProcessId;
  CloseHandle(pi.hThread);
  logger()->trace("Started '{}'", commandLine);

  // Slight delay to verify that process did not immediately die
  // for more clear and faster error response.
  this_thread::sleep_for(200ms);
  DWORD exitCode;
  if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
    CloseHandle(pi.hProcess);
    throw runtime_error("Process " + m_name + " is already dead. Aborting!");
  }
  CloseHandle(pi.hProcess);
  logger()->info("Process {} started", m_name);
  logger()->debug("Command: {}", commandLine);
}

bool Process::wait_for_exit(chrono::seconds timeout) {
  if (!m_pid) {
    return false;
  }

  HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, m_pid);
  if (!hProcess) {
    return false;
  }

  switch (WaitForSingleObject(
      hProcess, numeric_cast<DWORD>(
                    duration_cast<chrono::milliseconds>(timeout).count()))) {
    case WAIT_OBJECT_0:
      m_pid = 0;
      return true;
    case WAIT_TIMEOUT:
      return false;
    case WAIT_FAILED:
      logger()->error("Failed to wait for process - {}", GetLastError());
      return false;
    case WAIT_ABANDONED:
      logger()->error("Process abandoned");
      return false;
    default:
      logger()->error("Unknown return value from WaitForSingleObject");
      return false;
  }
}

void Process::terminate() {
  if (m_pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, m_pid);
    if (hProcess == nullptr) {
      logger()->error("Failed to open process for termination - {}",
                      GetLastError());
      return;
    }
    if (!TerminateProcess(hProcess, 0)) {
      logger()->error("Failed to terminate process - {}", GetLastError());
    } else {
      logger()->info("Process {} terminated", m_name);
    }
    CloseHandle(hProcess);

    // If stop_cmd, run and wait maximum 10sec, but we should wait no longer
    // than the process
    if (!m_stop_cmd.empty()) {
      Process stop_process("stop_" + m_name, m_stop_cmd);
      // Using 35s since 30s is the default max time docker use on windows
      if (!stop_process.wait_for_exit(35s)) {
        logger()->warn("Stop command did not exit within 35s");
      }
    }
  }
  m_pid = 0;
}

}  // namespace zit