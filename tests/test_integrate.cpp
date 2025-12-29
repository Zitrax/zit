/**
 * Currently using linux programs for integration testing
 * but inside docker containers.
 *
 * sudo apt install docker.io
 * sudo usermod -aG docker $USER
 * newgrp docker
 *
 * cd tests/docker
 * ./build.sh
 */

/**
 * Note that the integration tests depend on a third party tracker and seeder:
 *
 *  - Tracker (opentracker)
 *    - https://erdgeist.org/arts/software/opentracker/
 *  - Seeder (webtorrent-cli)
 *    - https://github.com/webtorrent/webtorrent-cli
 *
 * Both currently handled by the docker images built above.
 */

#include <cstdio>
#include <cstdlib>
#include <file_utils.hpp>
#include <file_writer.hpp>
#include <torrent.hpp>

#include "gtest/gtest.h"
#include "logger.hpp"
#include "process.hpp"
#include "test_main.hpp"
#include "test_utils.hpp"

// Path to the writer executable provided by CMake as a compile definition.
// Example: "C:/path/to/build/src/torrent_writer/zit_torrent_writer"
#ifndef TORRENT_WRITER_EXE
#error "TORRENT_WRITER_EXE not defined"
#endif

using namespace std;
using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace zit;
namespace fs = std::filesystem;

// constexpr auto TEST_BIND_ADDRESS{"192.168.0.18"};

class TestConfig : public zit::Config {
 public:
  TestConfig() : zit::Config() {}

  void set(IntSetting setting, int val) { m_int_settings[setting] = val; }
  void set(StringSetting setting, std::string val) {
    m_string_settings[setting] = val;
  }
};

namespace {

std::string get_host_ip_from_host() {
#ifndef WIN32
  if (const char* env = getenv("HOST_IP")) {
    if (strlen(env) > 0)
      return std::string(env);
  }
  // Try to obtain the host IP used for outbound traffic (robust on Linux)
  FILE* f = popen(
      "ip route get 1.1.1.1 2>/dev/null | awk '{for(i=1;i<=NF;i++) "
      "if($i==\"src\") {print $(i+1); exit}}'",
      "r");
  if (f) {
    char buf[128];
    if (fgets(buf, sizeof(buf), f)) {
      pclose(f);
      // strip newline
      std::string s(buf);
      if (!s.empty() && s.back() == '\n')
        s.pop_back();
      if (!s.empty())
        return s;
    } else {
      pclose(f);
    }
  }
  // Try docker bridge gateway as a fallback
  f = popen(
      "docker network inspect bridge --format '{{(index .IPAM.Config "
      "0).Gateway}}' 2>/dev/null",
      "r");
  if (f) {
    char buf[128];
    if (fgets(buf, sizeof(buf), f)) {
      pclose(f);
      std::string s(buf);
      if (!s.empty() && s.back() == '\n')
        s.pop_back();
      if (!s.empty())
        return s;
    } else {
      pclose(f);
    }
  }
#endif  // WIN32
  throw std::runtime_error("Failed to determine host IP address");
}

[[maybe_unused]] fs::path home_dir() {
  const auto home = getenv("HOME");
  if (!home || strlen(home) < 2) {
    throw runtime_error("HOME environment variable not set or invalid");
  }
  return {home};
}

// Generate a .torrent file from data using the test torrent writer
// `TORRENT_WRITER_EXE` is expected to be set by the build system.
fs::path generate_torrent_with_announce(const fs::path& data,
                                        const std::string& announce,
                                        const fs::path& out_dir) {
  const auto out_torrent =
      out_dir / ("gen_" + data.filename().string() + ".torrent");
  const std::string cmd = std::string(TORRENT_WRITER_EXE) + " --torrent " +
                          out_torrent.generic_string() + " --data " +
                          data.generic_string() +
                          " --comment 'Generated for testing' --announce " +
                          announce + " --piece-length 32768";
  // Use exec() from test_utils.hpp which throws on non-zero exit
  exec(cmd);
  return out_torrent;
}

// Ensure the test network exists for container communication
void ensure_test_network() {
  static bool network_created = false;
  if (network_created)
    return;

  try {
    // Remove any previous network to start fresh
    exec("docker network rm zit-test-network > /dev/null 2>&1 || true");
  } catch (const std::exception&) {
    // Ignore if network doesn't exist
  }

  try {
    exec("docker network create zit-test-network");
    network_created = true;
  } catch (const std::exception& e) {
    logger()->warn("Failed to create test network: {}", e.what());
    throw;
  }
}

auto start_tracker(const fs::path& data_dir) {
  // Ensure any previous tracker container is gone to avoid name conflicts
  try {
    exec("docker rm -f zit-opentracker > /dev/null 2>&1 || true");
  } catch (const std::exception&) {
    // Ignore cleanup errors
  }

  ensure_test_network();

  std::vector<std::string> args = {
      "docker", "run", "--rm", "--name", "zit-opentracker", "--network",
      "zit-test-network", "--volume",
      fmt::format("{}:{}", data_dir.generic_string(), "/data"),
      // Needed to be able to run iptables in the container
      "--cap-add", "NET_ADMIN",
      //
      "--publish", "8000:8000"};
  // Add host mapping
  const auto host_ip = get_host_ip_from_host();
  if (!host_ip.empty()) {
    args.push_back("--add-host");
    args.push_back(fmt::format("host.docker.internal:{}", host_ip));
  } else {
    args.push_back("--add-host");
    args.push_back("host.docker.internal:host-gateway");
  }
  args.push_back("opentracker");
  args.push_back("-p");
  args.push_back("8000");
  args.push_back("-w");
  args.push_back("/data/opentracker.whitelist");

  auto tracker =
      Process("tracker", args, nullptr, {"docker", "stop", "zit-opentracker"});

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

  // Ensure any previous tracker container is gone to avoid name conflicts
  try {
    exec(fmt::format("docker rm -f {} > /dev/null 2>&1 || true",
                     container_name));
  } catch (const std::exception&) {
    // Ignore cleanup errors
  }

  ensure_test_network();

  std::vector<std::string> args = {
      "docker", "run", "--rm", "--name", container_name, "--network",
      "zit-test-network",
#ifndef WIN32
      // Pass host UID/GID so container can fix file ownership on exit
      "--env", fmt::format("HOST_UID={}", getuid()), "--env",
      fmt::format("HOST_GID={}", getgid()),
#endif  // WIN32
      "--publish", fmt::format("{}:{}", port, port), "--volume",
      fmt::format("{}:{}", data_dir.generic_string(), container_data_dir),
      "--volume",
      fmt::format("{}:{}", torrent_file.parent_path().generic_string(),
                  container_torrent_dir),
      // Needed to be able to run iptables in the container
      "--cap-add", "NET_ADMIN",
      // For verbose output
      "--env",
      fmt::format("DEBUG={}",
                  logger()->should_log(spdlog::level::debug) ? "*" : "")};

  // Add host mapping so container can resolve host.docker.internal to host LAN
  // IP
  const auto host_ip = get_host_ip_from_host();
  if (!host_ip.empty()) {
    args.push_back("--add-host");
    args.push_back(fmt::format("host.docker.internal:{}", host_ip));
  } else {
    args.push_back("--add-host");
    args.push_back("host.docker.internal:host-gateway");
  }

  // Command and arguments for webtorrent
  args.push_back("webtorrent");
  args.push_back("seed");
  args.push_back(
      fmt::format("{}/{}", container_torrent_dir, torrent_file.filename()));
  args.push_back("--out");
  args.push_back(container_data_dir);
  args.push_back("--torrent-port");
  args.push_back(std::to_string(webtorrent_seeding_port++));
  args.push_back("--port");
  args.push_back(std::to_string(webtorrent_http_port++));
  args.push_back("--keep-seeding");

  return Process(name, args, nullptr, {"docker", "stop", container_name});
}

auto start_leecher(const fs::path& target, const fs::path& torrent_file) {
  try {
    if (fs::exists(target)) {
      // Ensure the directory is empty to avoid leftover files
      for (const auto& entry : fs::directory_iterator(target)) {
        fs::remove_all(entry);
      }
    }
    // Ensure the mount directory exists and is owned by the host user
    fs::create_directories(target);
  } catch (const std::exception& e) {
    logger()->warn("Failed preparing target directory {}: {}", target.string(),
                   e.what());
  }
  return start_seeder(target, torrent_file, "leecher");
}

// Zit-specific helpers for peer-to-peer testing
auto start_zit_seeder(const fs::path& data_dir,
                      const fs::path& torrent_file,
                      const std::string& name = "seeder") {
  static int zit_port = 6881;
  const auto port = std::to_string(zit_port++);
  const auto container_name = fmt::format("zit-test-{}", name);

  // Cleanup any previous container with this name
  try {
    exec(fmt::format("docker rm -f {} > /dev/null 2>&1 || true",
                     container_name));
  } catch (const std::exception&) {
    // Ignore cleanup errors
  }

  ensure_test_network();

  std::vector<std::string> args = {
      "docker", "run",
      "-t",  // Allocate pseudo-TTY to preserve color output
      "--rm", "--name", container_name, "--hostname", container_name,
      "--network", "zit-test-network",
#ifndef WIN32
      // Pass host UID/GID so container can fix file ownership on exit
      "--env", fmt::format("HOST_UID={}", getuid()), "--env",
      fmt::format("HOST_GID={}", getgid()),
#endif  // WIN32
      "--publish", fmt::format("{}:{}/tcp", port, port), "--publish",
      fmt::format("{}:{}/udp", port, port), "--volume",
      fmt::format("{}:/data", data_dir.generic_string()), "--volume",
      fmt::format("{}:/torrents", torrent_file.parent_path().generic_string()),
      // Needed to be able to run iptables in the container
      "--cap-add", "NET_ADMIN",
      // Forward SPDLOG_LEVEL for log level control
      "--env", "SPDLOG_LEVEL",
      // For verbose output
      "--env",
      fmt::format("DEBUG={}",
                  logger()->should_log(spdlog::level::debug) ? "1" : "0"),
      // Add host mapping
      "--add-host", "host.docker.internal:host-gateway", "zit", "--torrent",
      fmt::format("/torrents/{}", torrent_file.filename()), "--listening-port",
      port, "--log-prefix", name};

  const auto host_ip = get_host_ip_from_host();
  if (!host_ip.empty()) {
    // Insert host IP override before zit image name
    const auto insert_pos = static_cast<std::ptrdiff_t>(args.size()) - 7;
    args.insert(args.begin() + insert_pos, "--add-host");
    args.insert(args.begin() + insert_pos + 1,
                fmt::format("host.docker.internal:{}", host_ip));
  }

  return Process(name, args, nullptr, {"docker", "stop", container_name});
}

auto start_zit_leecher(const fs::path& target, const fs::path& torrent_file) {
  try {
    if (fs::exists(target)) {
      // Ensure the directory is empty to avoid leftover files
      for (const auto& entry : fs::directory_iterator(target)) {
        fs::remove_all(entry);
      }
    }
    // Ensure the mount directory exists and is owned by the host user
    fs::create_directories(target);
  } catch (const std::exception& e) {
    logger()->warn("Failed preparing target directory {}: {}", target.string(),
                   e.what());
  }
  return start_zit_seeder(target, torrent_file, "leecher");
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

class IntegrateF : public TestWithIOContext,
                   public TestWithTmpDir,
                   public ::testing::WithParamInterface<uint8_t> {};

#ifdef INTEGRATION_TESTS
TEST_P(IntegrateF, download) {
#else
TEST_P(IntegrateF, DISABLED_download) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto announce =
      std::format("http://{}:8000/announce", get_host_ip_from_host());
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "1MiB.dat", announce, tmp_dir());
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

class IntegrateOodF : public TestWithIOContext,
                      public TestWithFilesystem<1'500'000>,
                      public ::testing::WithParamInterface<uint8_t> {};

#ifdef INTEGRATION_TESTS
TEST_F(IntegrateOodF, download_ood) {
#else
TEST_F(IntegrateOodF, DISABLED_download_ood) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto announce =
      std::format("http://{}:8000/announce", get_host_ip_from_host());
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "1MiB.dat", announce, tmp_dir());
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

class Integrate : public TestWithIOContext, public TestWithTmpDir {};

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

  const auto torrent_file_1 = generate_torrent_with_announce(
      data_dir / "1MiB.dat",
      "http://" + get_host_ip_from_host() + ":8000/announce", tmp_dir());
  const auto torrent_file_2 = generate_torrent_with_announce(
      data_dir / "multi",
      "http://" + get_host_ip_from_host() + ":8000/announce", tmp_dir());

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
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "1MiB.dat",
      "http://" + get_host_ip_from_host() + ":8000/announce", tmp_dir());
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
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "multi",
      "http://" + get_host_ip_from_host() + ":8000/announce", tmp_dir());
  const auto download_dir = tmp_dir();

  auto tracker = start_tracker(data_dir);

  // Copy ready files to download_dir and modify one
  // such that it will be retransferred.
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
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "multi",
      "http://" + get_host_ip_from_host() + ":8000/announce", tmp_dir());

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
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "1MiB.dat",
      "http://" + get_host_ip_from_host() + ":8000/announce", tmp_dir());

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
  ASSERT_FALSE(fs::exists(destination))
      << "File already exists: " << destination;

  std::optional<zit::Process> leecher;
  asio::steady_timer timer{m_io_context};
  timer.expires_after(5s);

  timer.async_wait([&](auto ec) {
    EXPECT_FALSE(ec);
    leecher = start_leecher(target, torrent_file);

    torrent.set_not_interested_callback([&](zit::Peer*) {
      logger()->info("Peer not interested - upload complete, stopping");
      torrent.stop();
      leecher->terminate();
    });
  });

  // Connects to tracker and retrieves peers
  torrent.start();

  // Run the peer connections
  torrent.run();

  // Transfer done - Verify content
  verify_download(torrent, destination);
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, multi_upload) {
#else
TEST_F(Integrate, DISABLED_multi_upload) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "multi",
      "http://" + get_host_ip_from_host() + ":8000/announce", tmp_dir());

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
  const auto target = tmp_dir() / "multi_upload_test";
  const auto destination = target / "multi";  // folder name inside torrent
  // Be fully sure we do not have the files there yet
  ASSERT_FALSE(fs::exists(destination))
      << "File already exists: " << destination;

  std::optional<zit::Process> leecher;
  asio::steady_timer timer{m_io_context};
  timer.expires_after(15s);

  timer.async_wait([&](auto ec) {
    EXPECT_FALSE(ec);
    leecher = start_leecher(target, torrent_file);

    torrent.set_not_interested_callback([&](zit::Peer*) {
      logger()->info("Peer not interested - upload complete, stopping");
      torrent.stop();
      leecher->terminate();
    });
  });

  // Connects to tracker and retrieves peers
  torrent.start();

  // Run the peer connections
  torrent.run();

  // Transfer done - Verify content
  verify_download(torrent, destination, false);
}

#ifdef INTEGRATION_TESTS
TEST_F(Integrate, zit_to_zit_upload) {
#else
TEST_F(Integrate, DISABLED_zit_to_zit_upload) {
#endif  // INTEGRATION_TESTS
  const auto data_dir = fs::path(DATA_DIR);
  const auto torrent_file = generate_torrent_with_announce(
      data_dir / "multi",
      "http://" + std::string("zit-opentracker") + ":8000/announce", tmp_dir());

  auto tracker = start_tracker(data_dir);

  // Verify the source data exists before starting the test
  ASSERT_TRUE(fs::exists(data_dir / "multi"))
      << "Source data directory does not exist: " << data_dir / "multi";

  // Start a zit seeder in a Docker container with a different IP
  logger()->info("Starting zit seeder in Docker container");
  auto seeder = start_zit_seeder(data_dir, torrent_file);

  // Start a zit leecher in a separate Docker container to download
  const auto target = tmp_dir() / "zit_to_zit_test";
  const auto destination = target / "multi";  // folder name inside torrent

  // Ensure destination doesn't exist
  ASSERT_FALSE(fs::exists(destination))
      << "File already exists: " << destination;

  logger()->info("Starting zit leecher in Docker container");
  auto leecher = start_zit_leecher(target, torrent_file);

  // Wait for the Docker containers to complete the transfer
  // The seeder and leecher are running in Docker - no need for local torrent
  logger()->info("Waiting for zit-to-zit transfer to complete (max 120s)");

  // Poll for completion - check if leecher downloaded all files
  const auto start_time = std::chrono::steady_clock::now();
  const auto max_wait = 300s;
  bool transfer_complete = false;

  // The marker file is created alongside the destination directory
  // e.g. if destination is /tmp/foo/multi, marker is
  // /tmp/foo/multi.zit_downloading
  const auto marker_file =
      destination.string() + zit::Torrent::tmpfileExtension();
  ASSERT_FALSE(fs::exists(marker_file))
      << "Marker file already exists before download: " << marker_file;

  while (!transfer_complete &&
         (std::chrono::steady_clock::now() - start_time) < max_wait) {
    std::this_thread::sleep_for(1s);

    // Check if destination exists AND marker file is gone.
    // The marker file is removed only when the download is fully complete and
    // verified.
    if (fs::exists(destination) && !fs::exists(marker_file)) {
      transfer_complete = true;
    }
  }

  ASSERT_TRUE(transfer_complete) << "Transfer did not complete within timeout";

  // Verify the leecher downloaded the files
  logger()->info("Verifying /tmp/zit_**/zit_to_zit_test/multi and {}",
                 data_dir / "multi");
  ASSERT_TRUE(fs::exists(destination))
      << "Leecher did not download files to " << destination;

  // Verify content matches
  for (const auto& entry :
       fs::recursive_directory_iterator(data_dir / "multi")) {
    if (entry.is_regular_file()) {
      const auto relative = fs::relative(entry.path(), data_dir / "multi");
      const auto downloaded_file = destination / relative;
      ASSERT_TRUE(fs::exists(downloaded_file))
          << "Missing downloaded file: " << downloaded_file;
      ASSERT_EQ(fs::file_size(entry.path()), fs::file_size(downloaded_file))
          << "File size mismatch for: " << relative;

      auto source_sha1 = zit::Sha1::calculateFile(entry.path()).hex();
      auto target_sha1 = zit::Sha1::calculateFile(downloaded_file).hex();
      ASSERT_EQ(source_sha1, target_sha1)
          << "Content mismatch for: " << relative;
    }
  }

  logger()->info("zit-to-zit test passed!");
}
