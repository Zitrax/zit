#include "process.hpp"

#ifdef __linux__

#include <fmt/ranges.h>
#include <linux/prctl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include "logger.hpp"

namespace zit {

using namespace std;

Process::Process(const string& name, vector<const char*> argv, const char* cwd)
    : m_pid(0), m_name(name) {
  pid_t ppid = getpid();
  m_pid = fork();

  if (m_pid == -1) {
    throw runtime_error("Failed to fork for "s + argv[0] + " process");
  }

  if (m_pid == 0) {
    // Ensure that child is killed when parent dies
    int r = prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (r == -1) {
      perror("prctl failed");
      exit(1);
    }
    // Exit directly if parent is already dead
    if (getppid() != ppid) {
      exit(1);
    }
    if (cwd && chdir(cwd) == -1) {
      perror("chdir failed");
      exit(1);
    }
    argv.push_back(nullptr);
    execvp(argv[0], const_cast<char* const*>(argv.data()));
    cerr << "Failed launching " << argv[0] << ": " << strerror(errno) << endl;
    _exit(1);  // Important to use _exit(1) in the child branch
  } else {
    logger()->trace("Started '{}'", fmt::join(argv, " "));
    // Slight delay to verify that process did not immediately die
    // for more clear and faster error response.
    this_thread::sleep_for(200ms);
    int wstatus;
    auto status = waitpid(m_pid, &wstatus, WNOHANG | WUNTRACED);
    if (status < 0 || (status > 0 && WIFEXITED(wstatus))) {
      throw runtime_error("Process " + m_name + " is already dead. Aborting!");
    }
    logger()->info("Process {} started", m_name);
  }
}

void Process::terminate() {
  if (m_pid) {
    kill(m_pid, SIGTERM);
    logger()->info("Waiting for {}", m_name);
    int status;
    for (int i = 0; i < 500; ++i) {
      auto ret = waitpid(m_pid, &status, WNOHANG);
      if (ret > 0) {
        logger()->info("{} exited with status: {}", m_name,
                       WEXITSTATUS(status));
        m_pid = 0;
        return;
      }
      if (ret == -1) {
        logger()->error("waitpid errored - {}", strerror(errno));
        return;
      }
      std::this_thread::sleep_for(10ms);
    }
    logger()->info("{} still not dead, sending SIGKILL", m_name);
    kill(m_pid, SIGKILL);
    waitpid(m_pid, &status, 0);
    const auto fstatus = WEXITSTATUS(status);
    logger()->log(fstatus ? spdlog::level::warn : spdlog::level::info,
                  "{} exited with status: {}", m_name, fstatus);
  }
  m_pid = 0;
}

}  // namespace zit

#endif  // __linux___
