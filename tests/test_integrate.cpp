/**
 * Currently using linux programs for integration testing
 * but inside docker containers.
 */

/**
 * Note that the integration tests depend on a third party tracker and seeder:
 *
 *  - Tracker (opentracker)
 *    - https://erdgeist.org/arts/software/opentracker/
 *  - Seeder (transmission-cli)
 *    - sudo apt install transmission-cli
 */

#include <file_utils.hpp>
#include <file_writer.hpp>
#include <torrent.hpp>

#include "gtest/gtest.h"
#include "logger.hpp"
#include "process.hpp"
#include "test_main.hpp"
#include "test_utils.hpp"

using namespace std;
using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace zit;
namespace fs = std::filesystem;

// constexpr auto TEST_BIND_ADDRESS{"192.168.0.18"};

class TestConfig : public zit::Config {
 public:
  TestConfig() : zit::Config() {
    // As long as we use Transmission and test with all processes on localhost
    // we need to make sure we initiate the connections.
    // m_bool_settings[zit::BoolSetting::INITIATE_PEER_CONNECTIONS] = false;

    // Transmission does not want to connect to localhost, so trick it
    // m_string_settings[zit::StringSetting::BIND_ADDRESS] = TEST_BIND_ADDRESS;
  }

  void set(IntSetting setting, int val) { m_int_settings[setting] = val; }
  void set(StringSetting setting, std::string val) {
    m_string_settings[setting] = val;
  }
};

namespace {

[[maybe_unused]] fs::path home_dir() {
  const auto home = getenv("HOME");
  if (!home || strlen(home) < 2) {
    throw runtime_error("HOME environment variable not set or invalid");
  }
  return {home};
}

// FIXME: Document that "host" network has to be enabled in Docker Desktop.

auto start_tracker(const fs::path& data_dir) {
  auto tracker =
      Process("tracker",
              {"docker", "run", "--rm", "--name", "zit-opentracker", "--volume",
               fmt::format("{}:{}", data_dir.generic_string(), "/data"),
               //"--network", "host",
               "--publish", "8000:8000", "opentracker", "-p", "8000",
               // The default opentracker build seem to be compiled in closed
               // mode such that all torrent must be explicitly listed in a
               // whitelist. It would be possible to recompile it without that
               // but for now lets just use it like this.
               "-w", "/data/opentracker.whitelist"},
              nullptr, {"docker", "stop", "zit-opentracker"});

  // Allow some time for the tracker to start
  this_thread::sleep_for(1s);
  return tracker;
}

auto start_seeder(const fs::path& data_dir,
                  const fs::path& torrent_file,
                  const std::string& name = "seeder") {
  // Ensure to start each webtorrent instance on different ports
  static int webtorrent_seeding_port = 51413;
  static int webtorrent_http_port = 61413;

  // For docker we need to share a directory with the container
  constexpr auto* container_data_dir = "/data";
  constexpr auto* container_torrent_dir = "/torrents";

  static int seeder_count{0};
  const auto container_name = fmt::format("zit-webtorrent-{}", seeder_count++);

  const auto port = std::to_string(webtorrent_seeding_port);
  return Process(
      name,
      {"docker", "run", "--rm", "--name", container_name, "--publish",
       fmt::format("{}:{}", port, port), "--volume",
       fmt::format("{}:{}", data_dir.generic_string(), container_data_dir),
       "--volume",
       fmt::format("{}:{}", torrent_file.parent_path().generic_string(),
                   container_torrent_dir),
       "webtorrent", "seed",
       // Torrent file
       fmt::format("{}/{}", container_torrent_dir, torrent_file.filename()),
       "--out", container_data_dir, "--torrent-port",
       std::to_string(webtorrent_seeding_port++), "--port",
       std::to_string(webtorrent_http_port++), "--keep-seeding"},
      nullptr, {"docker", "stop", container_name});
}

/**
 * With transmission-cli leeching need to be done like this:
 *  - Start tracker
 *  - rm -rf ~/.config/transmission
 *  - rm eventual old file
 *  - seed with zit
 */
auto start_leecher(const fs::path& target, const fs::path& torrent_file) {
  fs::remove(target);
  return start_seeder(target, torrent_file, "leecher");
}

void start(zit::Torrent& torrent) {
  torrent.start();
  torrent.run();
}

void download(const fs::path& data_dir,
              std::vector<std::reference_wrapper<zit::Torrent>>& torrents,
              uint8_t number_of_seeders) {
  zit::FileWriterThread file_writer([](zit::Torrent& torrent) {
    logger()->info("Download completed of " + torrent.name());
    torrent.stop();
  });

  vector<Process> seeders;

  for (auto torrent_ref : torrents) {
    auto& torrent = torrent_ref.get();
    logger()->info("Starting {} seeders...", number_of_seeders);
    const auto torrent_file = torrent.torrent_file();
    for (int i = 0; i < number_of_seeders; ++i) {
      seeders.emplace_back(start_seeder(data_dir, torrent_file));
    }

    // Ensure we do not already have it
    if (torrent.is_single_file()) {
      fs::remove(torrent.name());
    }
    file_writer.register_torrent(torrent);
  }

  // Allow some time for the seeders to start
  // FIXME: Avoid this sleep
  this_thread::sleep_for(3s);

  std::vector<std::thread> torrent_threads;
  std::ranges::transform(
      torrents, std::back_inserter(torrent_threads), [](auto& torrent) {
        return std::thread([&]() {
          try {
            start(torrent);
          } catch (const std::exception& ex) {
            zit::logger()->error(
                "test_integrate::download: Exception in thread: {}", ex.what());
          }
        });
      });

  for (auto& torrent_thread : torrent_threads) {
    torrent_thread.join();
  }
}

void download(const fs::path& data_dir,
              zit::Torrent& torrent,
              uint8_t number_of_seeders) {
  std::vector<std::reference_wrapper<zit::Torrent>> torrents{torrent};
  download(data_dir, torrents, number_of_seeders);
}

void verify_download(const zit::Torrent& torrent,
                     const fs::path& source,
                     bool clean = true) {
  const auto name = torrent.name();

  logger()->info("Verifying {} and {}", source, name);

  if (torrent.is_single_file()) {
    auto source_sha1 = zit::Sha1::calculateFile(source).hex();
    auto target_sha1 = zit::Sha1::calculateFile(name).hex();
    EXPECT_EQ(source_sha1, target_sha1);
  } else {
    for (const auto& fi : torrent.files()) {
      auto file_source = source / fi.path();
      auto dst = name / fi.path();
      auto source_sha1 = zit::Sha1::calculateFile(file_source).hex();
      auto target_sha1 = zit::Sha1::calculateFile(dst).hex();
      EXPECT_EQ(source_sha1, target_sha1) << fi.path();
      // Delete downloaded file
      if (clean) {
        fs::remove(dst);
      }
    }
    if (clean) {
      fs::remove(name);
    }
  }
}

}  // namespace

class IntegrateF : public TestWithContext,
                   public TestWithTmpDir,
                   public ::testing::WithParamInterface<uint8_t> {};

#ifdef INTEGRATION_TESTS
TEST_P(IntegrateF, download) {
#else
TEST_P(IntegrateF, DISABLED_download) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "1MiB.torrent";
  const uint8_t number_of_seeders = GetParam();

  auto tracker = start_tracker(data_dir);

  const auto download_dir = tmp_dir();
  TestConfig test_config;
  zit::Torrent torrent(m_io_context, torrent_file, download_dir, test_config);
  ASSERT_FALSE(torrent.done());
  download(data_dir, {torrent}, number_of_seeders);
  const auto target = torrent.name();

  // Transfer done - Verify content
  verify_download(torrent, data_dir / "1MiB.dat");
}

INSTANTIATE_TEST_SUITE_P(SeedCount,
                         IntegrateF,
                         ::testing::Values<uint8_t>(1, 2, 5, 10));

// OOD test is linux only at the moment
#ifndef WIN32

class IntegrateOodF : public TestWithContext,
                      public TestWithFilesystem<1'500'000>,
                      public ::testing::WithParamInterface<uint8_t> {};

#ifdef INTEGRATION_TESTS
TEST_F(IntegrateOodF, download_ood) {
#else
TEST_F(IntegrateOodF, DISABLED_download_ood) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "1MiB.torrent";
  constexpr uint8_t number_of_seeders = 1;

  auto tracker = start_tracker(data_dir);

  const auto download_dir = mount_dir();
  TestConfig test_config;
  zit::Torrent torrent(m_io_context, torrent_file, download_dir, test_config);

  sigint_function = [&](int /*s*/) {
    logger()->warn("CTRL-C pressed. Stopping torrent...");
    torrent.stop();
  };

  // Test the case where we can store the file once
  logger()->info("Available: {}, Torrent length: {}", available(),
                 torrent.length());
  ASSERT_GT(available(), torrent.length());
  ASSERT_LT(available(), 2 * torrent.length());

  ASSERT_FALSE(torrent.done());
  download(data_dir, torrent, number_of_seeders);
  const auto target = torrent.name();

  // Transfer done - Verify content
  verify_download(torrent, data_dir / "1MiB.dat");
}

#endif  // WIN32

class Integrate : public TestWithContext, public TestWithTmpDir {};

// Verify we can download two torrents at the same time
#ifdef INTEGRATION_TESTS
TEST_F(IntegrateF, download_dual_torrents) {
#else
TEST_F(IntegrateF, DISABLED_download_dual_torrents) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  constexpr uint8_t number_of_seeders = 1;
  const auto download_dir = tmp_dir();
  TestConfig test_config;

  const auto torrent_file_1 = data_dir / "1MiB.torrent";
  const auto torrent_file_2 = data_dir / "multi.torrent";

  auto tracker = start_tracker(data_dir);

  zit::Torrent torrent_1(m_io_context, torrent_file_1, download_dir,
                         test_config);
  // FIXME: This is not what we want in the end. Without this we currently
  //        try to start multiple listeners on the same port which will
  //        not work. One port per torrent does not scale.
  // test_config.set(IntSetting::LISTENING_PORT, 20002);
  zit::Torrent torrent_2(m_io_context, torrent_file_2, download_dir,
                         test_config);

  ASSERT_FALSE(torrent_1.done());
  ASSERT_FALSE(torrent_2.done());

  std::vector<std::reference_wrapper<zit::Torrent>> torrents{torrent_1,
                                                             torrent_2};
  download(data_dir, torrents, number_of_seeders);

  // Transfer done - verify contents
  verify_download(torrent_1, data_dir / "1MiB.dat");
  verify_download(torrent_2, data_dir / torrent_2.name());
}

// This test verifies that we can resume a download
// where one piece is missing
#ifdef INTEGRATION_TESTS
TEST_F(Integrate, download_part) {
#else
TEST_P(IntegrateF, DISABLED_download_part) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "1MiB.torrent";
  const auto download_dir = tmp_dir();

  auto tracker = start_tracker(data_dir);

  // Copy ready file to download_dir and modify a piece
  // such that it will be retransferred.
  const auto fn = download_dir / "1MiB.dat" += zit::Torrent::tmpfileExtension();
  fs::copy_file(data_dir / "1MiB.dat", fn);
  auto content = zit::read_file(fn);
  constexpr auto byte_to_change = 300'000;
  ASSERT_TRUE(content.at(byte_to_change) != 0);
  content.at(byte_to_change) = 0;
  zit::write_file(fn, content);

  TestConfig test_config;
  zit::Torrent torrent(m_io_context, torrent_file, download_dir, test_config);
  ASSERT_FALSE(torrent.done());

  unsigned pieces_downloaded = 0;
  torrent.add_piece_callback(
      [&pieces_downloaded](auto, auto) { pieces_downloaded++; });

  download(data_dir, torrent, 1);
  const auto target = torrent.name();

  // Transfer done - Verify content
  verify_download(torrent, data_dir / "1MiB.dat");

  // Since we only changed one piece we should only have to get one piece again
  EXPECT_GT(torrent.pieces().size(), 1);
  EXPECT_EQ(pieces_downloaded, 1);
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, download_multi_part) {
#else
TEST_F(Integrate, DISABLED_download_multi_part) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "multi.torrent";
  const auto download_dir = tmp_dir();

  auto tracker = start_tracker(data_dir);

  // Copy ready files to download_dir and modify one
  // such that it will be retranfered.
  fs::copy(data_dir / "multi", download_dir / "multi");
  zit::write_file(download_dir / "multi" += zit::Torrent::tmpfileExtension(),
                  "");
  const auto fn = download_dir / "multi" / "b";
  auto content = zit::read_file(fn);
  constexpr auto byte_to_change = 500;
  ASSERT_TRUE(content.at(byte_to_change) != 0);
  content.at(byte_to_change) = 0;
  zit::write_file(fn, content);

  TestConfig test_config;
  zit::Torrent torrent(m_io_context, torrent_file, download_dir, test_config);
  ASSERT_FALSE(torrent.done());

  unsigned pieces_downloaded = 0;
  torrent.add_piece_callback(
      [&pieces_downloaded](auto, auto) { pieces_downloaded++; });

  download(data_dir, torrent, 1);
  const auto target = torrent.name();

  // Transfer done - Verify content
  verify_download(torrent, data_dir / torrent.name());

  // Since we only changed one piece we should only have to get one piece again
  EXPECT_GT(torrent.pieces().size(), 1);
  EXPECT_EQ(pieces_downloaded, 1);
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, download_multi_file) {
#else
TEST_F(Integrate, DISABLED_download_multi_file) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "multi.torrent";

  auto tracker = start_tracker(data_dir);

  const auto download_dir = tmp_dir();
  TestConfig test_config;
  zit::Torrent torrent(m_io_context, torrent_file, download_dir, test_config);
  ASSERT_FALSE(torrent.done());
  download(data_dir, torrent, 1);
  const auto target = torrent.name();

  // Transfer done - Verify content
  verify_download(torrent, data_dir / torrent.name());
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, upload) {
#else
TEST_F(Integrate, DISABLED_upload) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "1MiB.torrent";

  auto tracker = start_tracker(data_dir);

  // Launch zit with existing file to seed it
  TestConfig test_config;
  zit::Torrent torrent(m_io_context, torrent_file, data_dir, test_config);
  ASSERT_TRUE(torrent.done());

  sigint_function = [&](int /*s*/) {
    logger()->warn("CTRL-C pressed. Stopping torrent...");
    torrent.stop();
  };

  // Start a leecher that we will upload to
  const auto target = tmp_dir() / "upload_test";
  const auto destination = target / "1MiB.dat";
  // Be fully sure we do not have the file there yet
  ASSERT_FALSE(fs::exists(destination));

  std::optional<zit::Process> leecher;
  asio::steady_timer timer{m_io_context};
  timer.expires_after(5s);

  timer.async_wait([&](auto ec) {
    EXPECT_FALSE(ec);
    leecher = start_leecher(target, torrent_file);

    torrent.set_disconnect_callback([&](zit::Peer*) {
      logger()->info("Peer disconnect - stopping");
      torrent.stop();
      leecher->terminate();
    });
  });

  // FIXME: How to avoid this sleep?
  // this_thread::sleep_for(5s);

  // Connects to tracker and retrieves peers
  torrent.start();

  // Run the peer connections
  m_io_context.run();

  // Transfer done - Verify content
  verify_download(torrent, destination);
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, multi_upload) {
#else
TEST_F(Integrate, DISABLED_multi_upload) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = data_dir / "multi.torrent";

  auto tracker = start_tracker(data_dir);

  // Launch zit with existing file to seed it
  TestConfig test_config;
  zit::Torrent torrent(m_io_context, torrent_file, data_dir, test_config);
  ASSERT_TRUE(torrent.done());

  // Start a leecher that we will upload to
  auto target = tmp_dir() / "multi_upload_test";
  auto leecher = start_leecher(target, torrent_file);

  torrent.set_disconnect_callback([&](zit::Peer*) {
    logger()->info("Peer disconnect - stopping");
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
  verify_download(torrent, data_dir / torrent.name(), false);
}
