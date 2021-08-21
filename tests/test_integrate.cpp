/**
 * Currently using linux programs for integration testing and
 * linux system call to launch them.
 */
#ifdef __linux__

/**
 * Note that the integration tests depend on a third party tracker and seeder:
 *
 *  - Tracker (opentracker)
 *    - https://erdgeist.org/arts/software/opentracker/
 *  - Seeder (transmission-cli)
 *    - sudo apt install transmission-cli
 */

#include <sys/prctl.h>
#include <unistd.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>

#include <file_writer.h>
#include <torrent.h>

#include "gtest/gtest.h"

using namespace std;
using namespace std::chrono_literals;
using namespace std::string_literals;
namespace fs = std::filesystem;

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
      execvp(argv[0], const_cast<char* const*>(argv.data()));
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
      auto console = spdlog::get("console");
      console->info("Process {} started", m_name);
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
  void terminate() {
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
          m_pid = 0;
          return;
        }
        if (ret == -1) {
          console->error("waitpid errored - {}", strerror(errno));
          return;
        }
        std::this_thread::sleep_for(10ms);
      }
      console->info("{} still not dead, sending SIGKILL", m_name);
      kill(m_pid, SIGKILL);
      waitpid(m_pid, &status, 0);
      console->info("{} exited with status: {}", m_name, WEXITSTATUS(status));
    }
    m_pid = 0;
  }

  /**
   * Kill the process if not already dead.
   */
  ~Process() { terminate(); }

 private:
  pid_t m_pid;
  string m_name;
};

static auto start_tracker() {
  auto tracker = Process(
      "tracker", {"/home/danielb/git/opentracker/opentracker", "-p", "8000"},
      nullptr);

  // Allow some time for the tracker to start
  this_thread::sleep_for(1s);
  return tracker;
}

static auto start_seeder(const fs::path& data_dir,
                         const fs::path& torrent_file) {
  // FIXME: get hold of the home dir properly
  fs::remove_all("/home/danielb/.config/transmission");
  return Process("leecher", {"transmission-cli", "-w", data_dir.c_str(),
                             torrent_file.c_str()});
}

/**
 * With transmission-cli leeching need to be done like this:
 *  - Start tracker
 *  - rm -rf ~/.config/transmission
 *  - rm eventual old file
 *  - seed with zit
 */
static auto start_leecher(const fs::path& target,
                          const fs::path& torrent_file) {
  fs::remove(target);
  return start_seeder(target, torrent_file);
}

static void start(zit::Torrent& torrent) {
  torrent.start();
  torrent.run();
}

class TemporaryDir : public ::testing::Test {
 public:
  TemporaryDir() {
    if (!mkdtemp(m_dirname.data())) {
      throw runtime_error("Could not create temporary directory");
    }
    m_created = true;
  }

  ~TemporaryDir() override {
    if (m_created) {
      fs::remove_all(m_dirname.data());
    }
  }

  /**
   * The temporary directory when running the test.
   */
  [[nodiscard]] fs::path tmp_dir() const { return m_dirname.data(); }

 private:
  bool m_created = false;
  array<char, 16> m_dirname{'/', 't', 'm', 'p', '/', 'z', 'i', 't',
                            '_', 'X', 'X', 'X', 'X', 'X', 'X', '\0'};
};

static auto download(const fs::path& data_dir,
                     const fs::path& torrent_file,
                     zit::Torrent& torrent,
                     uint8_t max) {
  spdlog::get("console")->info("Starting {} seeders...", max);
  vector<Process> seeders;
  for (int i = 0; i < max; ++i) {
    seeders.emplace_back(start_seeder(data_dir, torrent_file));
  }

  // Allow some time for the seeders to start
  // FIXME: Avoid this sleep
  this_thread::sleep_for(15s);

  // Download torrent with zit
  auto target = torrent.name();

  // Ensure we do not already have it
  fs::remove(target);

  zit::FileWriterThread file_writer(torrent, [&torrent](zit::Torrent&) {
    spdlog::get("console")->info("Download completed");
    torrent.stop();
  });
  start(torrent);
  return target;
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

  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "1MiB.torrent";
  const uint8_t max = GetParam();

  zit::Torrent torrent(torrent_file);
  auto target = download(data_dir, torrent_file, torrent, max);

  // Transfer done - Verify content
  auto source = data_dir / "1MiB.dat";
  auto source_sha1 = zit::Sha1::calculateFile(source).hex();
  auto target_sha1 = zit::Sha1::calculateFile(target).hex();
  EXPECT_EQ(source_sha1, target_sha1);
}

INSTANTIATE_TEST_SUITE_P(SeedCount,
                         IntegrateF,
                         ::testing::Values<uint8_t>(1, 2, 5, 10));

using Integrate = TemporaryDir;

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, download_multi) {
#else
TEST_F(Integrate, DISABLED_download_multi) {
#endif  // INTEGRATION_TESTS
  spdlog::get("console")->set_level(spdlog::level::info);
  auto tracker = start_tracker();

  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "multi.torrent";
  // const uint8_t max = GetParam();

  zit::Torrent torrent(torrent_file);
  auto target = download(data_dir, torrent_file, torrent, 1);
  std::cout << "TARGET: " << target << std::endl;

  // Transfer done - Verify content
  const auto name = torrent.name();
  for (const auto& fi : torrent.files()) {
    auto source = data_dir / name / fi.path();
    auto dst = name / fi.path();
    auto source_sha1 = zit::Sha1::calculateFile(source).hex();
    auto target_sha1 = zit::Sha1::calculateFile(dst).hex();
    EXPECT_EQ(source_sha1, target_sha1) << fi.path();
    // Delete downloaded file
    fs::remove(dst);
  }
  fs::remove(name);
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, upload) {
#else
TEST_F(Integrate, DISABLED_upload) {
#endif  // INTEGRATION_TESTS
  spdlog::get("console")->set_level(spdlog::level::info);
  auto tracker = start_tracker();

  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "1MiB.torrent";

  // Launch zit with existing file to seed it
  zit::Torrent torrent(torrent_file, data_dir);
  ASSERT_TRUE(torrent.done());

  // Start a leecher that we will upload to
  auto target = tmp_dir() / "upload_test";
  auto leecher = start_leecher(target, torrent_file);

  torrent.set_disconnect_callback([&](zit::Peer*) {
    spdlog::get("console")->info("Peer disconnect - stopping");
    torrent.stop();
    leecher.terminate();
  });

  // FIXME: How to avoid this sleep?
  this_thread::sleep_for(15s);

  // Connects to tracker and retrieves peers
  torrent.start();

  // Run the peer connections
  torrent.run();

  // Transfer done - Verify content
  auto source = data_dir / "1MiB.dat";
  auto source_sha1 = zit::Sha1::calculateFile(source).hex();
  auto target_sha1 = zit::Sha1::calculateFile(target / "1MiB.dat").hex();
  EXPECT_EQ(source_sha1, target_sha1);
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, multi_upload) {
#else
TEST_F(Integrate, DISABLED_multi_upload) {
#endif  // INTEGRATION_TESTS
  spdlog::get("console")->set_level(spdlog::level::trace);
  auto tracker = start_tracker();

  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "multi.torrent";

  // Launch zit with existing file to seed it
  zit::Torrent torrent(torrent_file, data_dir);
  ASSERT_TRUE(torrent.done());

  // Start a leecher that we will upload to
  auto target = tmp_dir() / "multi_upload_test";
  auto leecher = start_leecher(target, torrent_file);

  torrent.set_disconnect_callback([&](zit::Peer*) {
    spdlog::get("console")->info("Peer disconnect - stopping");
    torrent.stop();
    leecher.terminate();
  });

  // FIXME: How to avoid this sleep?
  this_thread::sleep_for(15s);

  // Connects to tracker and retrieves peers
  torrent.start();

  // Run the peer connections
  torrent.run();

  // Transfer done - Verify content
  const auto name = torrent.name();
  EXPECT_EQ(torrent.files().size(), 7);
  for (const auto& fi : torrent.files()) {
    auto source = data_dir / name / fi.path();
    auto dst = target / name / fi.path();
    auto source_sha1 = zit::Sha1::calculateFile(source).hex();
    auto target_sha1 = zit::Sha1::calculateFile(dst).hex();
    EXPECT_EQ(source_sha1, target_sha1) << fi.path();
  }
}

#endif  // __linux__
