/**
 * Currently using linux programs for integration testing and
 * linux system call to launch them.
 */
#ifdef __linux__

#include <sys/prctl.h>
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <thread>

#include <file_writer.h>
#include <torrent.h>

#include "gtest/gtest.h"

using namespace std;
using namespace std::chrono_literals;
using namespace std::string_literals;

/**
 * Launch background process and return it's pid.
 */
static pid_t start_process(vector<const char*> argv,
                           const char* cwd = nullptr) {
  pid_t ppid = getpid();
  pid_t pid = fork();

  if (pid == -1) {
    throw runtime_error("Failed to fork for "s + argv[0] + " process");
  }

  if (pid == 0) {
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
    if (cwd) {
      chdir(cwd);
    }
    argv.push_back(nullptr);
    int status = execvp(argv[0], const_cast<char* const*>(argv.data()));
    cerr << "Failed launching " << argv[0] << ": " << strerror(errno) << endl;
    exit(1);
  }

  return pid;
}

TEST(integrate, transfer) {
  auto console = spdlog::get("console");
  console->set_level(spdlog::level::info);

  // Launch tracker
  pid_t tracker = start_process({"bittorrent-tracker", "--http"});

  // Allow some time for the tracker to start
  this_thread::sleep_for(1s);

  // Seed the torrent
  filesystem::path p(__FILE__);
  auto data_dir = p.parent_path() / "data";
  auto torrent_file = data_dir / "1MiB.torrent";
  pid_t seeder =
      start_process({"ctorrent", torrent_file.c_str()}, data_dir.c_str());

  // Allow some time for the seeder to start
  this_thread::sleep_for(1s);

  // Download torrent with zit
  zit::Peer* peer = nullptr;
  zit::Torrent torrent(torrent_file);
  zit::FileWriterThread file_writer(torrent, [&console, &peer](zit::Torrent&) {
    console->info("Download completed");
    if (peer) {
      peer->stop();
    }
  });
  auto peers = torrent.start();
  for (auto& p : peers) {
    if (!(p.url().host() == "127.0.0.1" && p.url().port() == p.port())) {
      peer = &p;
      // TODO: If the transfer does not finish for some reason we will hang
      //       here. Should have some timeout functionality. googletest
      //       does not have anything for that so I need to implement it.
      //       See: https://github.com/google/googletest/issues/348
      p.handshake(torrent.info_hash());
      break;
    }
  }

  // Transfer done
  // TODO: Verify checksum of original and downloaded file
  //       Either here or perhaps in zit itself.

  // Stop seeder
  kill(seeder, SIGTERM);
  cout << "Waiting for seeder" << endl;
  int status;
  waitpid(seeder, &status, 0);
  cout << "Seeder exited with status: " << status << endl;

  // Stop tracker
  kill(tracker, SIGTERM);
  cout << "Waiting for tracker" << endl;
  waitpid(tracker, &status, 0);
  cout << "Tracker exited with status: " << status << endl;
}

#endif  // __linux__
