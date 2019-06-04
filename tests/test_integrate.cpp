/**
 * Currently using linux programs for integration testing and
 * linux system call to launch them.
 */
#ifdef __linux__

/**
 * Note that the integration tests depend on a third party tracker and seeder:
 *
 *  - Tracker (bittorrent-tracker)
 *    - sudo npm install -g bittorrent-tracker
 *  - Seeder (ctorrent)
 *    - sudo apt install ctorrent
 */

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
 * Launch background process which is stopped/killed by the destructor.
 */
class Process {
 public:
  Process(const string& name,
          vector<const char*> argv,
          const char* cwd = nullptr)
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
      int status = execvp(argv[0], const_cast<char* const*>(argv.data()));
      cerr << "Failed launching " << argv[0] << ": " << strerror(errno) << endl;
      exit(1);
    } else {
      // Slight delay to verify that process did not immediately die
      // for more clear and faster error response.
      this_thread::sleep_for(200ms);
      int wstatus;
      auto status = waitpid(m_pid, &wstatus, WNOHANG | WUNTRACED);
      if (status < 0 || (status > 0 && WIFEXITED(wstatus))) {
        throw runtime_error("Process " + m_name +
                            " is already dead. Aborting!");
      }
    }
  }

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
  ~Process() {
    if (m_pid) {
      auto console = spdlog::get("console");
      kill(m_pid, SIGTERM);
      console->info("Waiting for {}", m_name);
      int status;
      for (int i = 0; i < 500; ++i) {
        auto ret = waitpid(m_pid, &status, WNOHANG);
        if (ret > 0) {
          console->info("{} exited with status: {}", m_name,
                        WEXITSTATUS(status));
          return;
        }
        if (ret == -1) {
          console->error("waitpid errored");
          return;
        }
        std::this_thread::sleep_for(10ms);
      }
      console->info("{} still not dead, sending SIGKILL", m_name);
      kill(m_pid, SIGKILL);
      waitpid(m_pid, &status, 0);
      console->info("{} exited with status: {}", m_name, WEXITSTATUS(status));
    }
  }

 private:
  pid_t m_pid;
  string m_name;
};

static auto start_tracker() {
  auto tracker = Process("tracker", {"bittorrent-tracker", "--http"}, nullptr);
  // Allow some time for the tracker to start
  this_thread::sleep_for(1s);
  return tracker;
}

static auto start_seeder(const std::filesystem::path& data_dir,
                         const std::filesystem::path& torrent_file) {
  return Process("seeder", {"ctorrent", "-v", torrent_file.c_str()},
                 data_dir.c_str());
}

static auto start_leecher(const std::filesystem::path& torrent_file) {
  // TODO: Might want to use a temporary cwd for this
  auto bf = torrent_file;
  bf += ".bf";
  filesystem::remove(bf);
  filesystem::remove("zzz");
  return Process("leecher", {"ctorrent", "-s", "zzz", torrent_file.c_str()});
}

static void start(zit::Torrent& torrent) {
  torrent.start();
  torrent.run();
}

class IntegrateF : public ::testing::Test,
                   public ::testing::WithParamInterface<uint8_t> {};

#ifdef INTEGRATION_TESTS
TEST_P(IntegrateF, download) {
#else
TEST_P(IntegrateF, DISABLED_download) {
#endif  // INTEGRATION_TESTS
  spdlog::get("console")->set_level(spdlog::level::info);
  auto tracker = start_tracker();

  filesystem::path p(__FILE__);
  auto data_dir = p.parent_path() / "data";
  auto torrent_file = data_dir / "1MiB.torrent";

  const uint8_t max = GetParam();
  spdlog::get("console")->info("Starting {} seeders...", max);
  vector<Process> seeders;
  for (int i = 0; i < max; ++i) {
    seeders.emplace_back(start_seeder(data_dir, torrent_file));
  }

  // Allow some time for the seeders to start
  this_thread::sleep_for(1s);

  // Download torrent with zit
  zit::Torrent torrent(torrent_file);
  auto target = torrent.name();

  // Ensure we do not already have it
  filesystem::remove(target);

  zit::FileWriterThread file_writer(torrent, [&torrent](zit::Torrent&) {
    spdlog::get("console")->info("Download completed");
    for_each(torrent.peers().begin(), torrent.peers().end(),
             [](auto& peer) { peer->stop(); });
  });
  start(torrent);

  // Transfer done - Verify content
  auto source = data_dir / "1MiB.dat";
  auto source_sha1 = zit::Sha1::calculateFile(source).hex();
  auto target_sha1 = zit::Sha1::calculateFile(target).hex();
  EXPECT_EQ(source_sha1, target_sha1);

  // Delete downloaded file
  filesystem::remove(target);
}

INSTANTIATE_TEST_SUITE_P(SeedCount,
                         IntegrateF,
                         ::testing::Values<uint8_t>(1, 2, 5, 10));

#ifdef INTEGRATION_TESTS
TEST(Integrate, upload) {
#else
TEST(Integrate, DISABLED_upload) {
#endif  // INTEGRATION_TESTS
  spdlog::get("console")->set_level(spdlog::level::info);
  auto tracker = start_tracker();

  filesystem::path p(__FILE__);
  auto data_dir = p.parent_path() / "data";
  auto torrent_file = data_dir / "1MiB.torrent";

  // Launch zit with existing file to seed it
  zit::Torrent torrent(torrent_file, data_dir);
  ASSERT_TRUE(torrent.done());

  torrent.set_disconnect_callback([](zit::Peer* peer) {
    spdlog::get("console")->info("Peer disconnect - stopping");
    peer->stop();
  });
  // Connects to tracker and retrieves peers
  torrent.start();
  // Start a leecher that we will upload to
  auto leecher = start_leecher(torrent_file);
  // Run the peer connections
  torrent.run();

  // Transfer done - Verify content
  auto source = data_dir / "1MiB.dat";
  auto target = "zzz";  // FIXME: fix name and location of this
  auto source_sha1 = zit::Sha1::calculateFile(source).hex();
  auto target_sha1 = zit::Sha1::calculateFile(target).hex();
  EXPECT_EQ(source_sha1, target_sha1);

  // Delete downloaded file
  filesystem::remove(target);
}

#endif  // __linux__
