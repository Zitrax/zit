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
    if (cwd && chdir(cwd) == -1) {
      perror("chdir failed");
      exit(1);
    }
    argv.push_back(nullptr);
    int status = execvp(argv[0], const_cast<char* const*>(argv.data()));
    cerr << "Failed launching " << argv[0] << ": " << strerror(errno) << endl;
    exit(1);
  }

  return pid;
}

static auto start_tracker() {
  pid_t tracker = start_process({"bittorrent-tracker", "--http"});
  // Allow some time for the tracker to start
  this_thread::sleep_for(1s);
  return tracker;
}

static auto start_seeder(const std::filesystem::path& data_dir,
                         const std::filesystem::path& torrent_file) {
  return start_process({"ctorrent", torrent_file.c_str()}, data_dir.c_str());
}

static void start(zit::Torrent& torrent) {
  torrent.start();
  for (auto& p : torrent.peers()) {
    p.handshake(torrent.info_hash());
  }
  torrent.run();
}

#ifdef INTEGRATION_TESTS
TEST(integrate, download) {
#else
TEST(integrate, DISABLED_download) {
#endif  // INTEGRATION_TESTS
  auto console = spdlog::get("console");
  console->set_level(spdlog::level::info);

  pid_t tracker = start_tracker();

  filesystem::path p(__FILE__);
  auto data_dir = p.parent_path() / "data";
  auto torrent_file = data_dir / "1MiB.torrent";

  pid_t seeder = start_seeder(data_dir, torrent_file);

  // Allow some time for the seeder to start
  this_thread::sleep_for(1s);

  // Download torrent with zit
  zit::Torrent torrent(torrent_file);
  zit::FileWriterThread file_writer(
      torrent, [&console, &torrent](zit::Torrent&) {
        console->info("Download completed");
        for_each(torrent.peers().begin(), torrent.peers().end(),
                 [](auto& peer) { peer.stop(); });
      });
  start(torrent);

  // Transfer done - Verify content
  auto source = data_dir / "1MiB.dat";
  auto target = torrent.tmpfile();
  auto source_sha1 = zit::Sha1::calculate(source).hex();
  auto target_sha1 = zit::Sha1::calculate(target).hex();
  EXPECT_EQ(source_sha1, target_sha1);

  // Delete downloaded file
  filesystem::remove(target);

  // Stop seeder
  kill(seeder, SIGTERM);
  console->info("Waiting for seeder");
  int status;
  waitpid(seeder, &status, 0);
  console->info("Seeder exited with status: {}", status);

  // Stop tracker
  kill(tracker, SIGTERM);
  console->info("Waiting for tracker");
  waitpid(tracker, &status, 0);
  console->info("Tracker exited with status: {}", status);
}

#endif  // __linux__
