// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <filesystem>

#include <stdio.h>
#include <array>
#include <asio/io_context.hpp>
#include <regex>
#include <stdexcept>
#include <string>
#include "gtest/gtest.h"
#include "logger.hpp"
#include "random.hpp"
#include "spdlog/fmt/fmt.h"
#include "spdlog/spdlog.h"

class TestWithIOContext {
 public:
  asio::io_context m_io_context{};
};

class TestWithTmpDir : public ::testing::Test {
 public:
  TestWithTmpDir() {
    if (!std::filesystem::create_directory(m_dirname)) {
      throw std::runtime_error("Could not create temporary directory: " +
                               m_dirname.string());
    }
    m_created = true;
  }

  ~TestWithTmpDir() override {
    if (m_created) {
      try {
        std::filesystem::remove_all(m_dirname);
      } catch (const std::exception& ex) {
        try {
          std::cerr << "WARN: Could not cleanup " << m_dirname << ": "
                    << ex.what() << "\n";
        } catch (...) {
        }
      }
    }
  }

  /**
   * The temporary directory when running the test.
   */
  [[nodiscard]] std::filesystem::path tmp_dir() const { return m_dirname; }

 private:
  bool m_created = false;
  std::filesystem::path m_dirname = std::filesystem::temp_directory_path() /
                                    ("zit_" + zit::random_string(10));
};

inline std::string exec(const std::string& cmd) {
#ifdef WIN32
#define popen _popen
#define pclose _pclose
#endif  // WIN32

  zit::logger()->debug("Running: {}", cmd);
  const auto pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }

  std::array<char, 128> buffer;
  std::string result;
  while (!feof(pipe)) {
    if (fgets(buffer.data(), 128, pipe) != nullptr) {
      result += buffer.data();
    }
  }

  const auto rc = pclose(pipe);
  zit::logger()->debug("RC = {} for {} ", rc, cmd);

  if (rc != EXIT_SUCCESS) {
    throw std::runtime_error(
        fmt::format("Command '{}' failed with exit status {}", cmd, rc));
  }
  return result;

#ifdef WIN32
#undef popen
#undef pclose
#endif  // WIN32
}

#ifndef WIN32

/**
 * Creates a file backed filesystem with a specific size intially aimed at
 * testing OOD situations.
 *
 * Note for ext4 systems with everything default you seem to need at least
 * around 250KB for the filesystem to be created without errors. To avoid
 * warnings about journaling not working you need at least around 9MB - but no
 * journal is not an error.
 *
 * For now this implementation is linux specific and use std::system to run
 * commands.
 *
 * @note TestWithFilesystem is currently linux only by using mkfs.ext4 and
 * udisksctl
 */
template <size_t FS_SIZE_IN_BYTES>
class TestWithFilesystem : public TestWithTmpDir {
 public:
  TestWithFilesystem() : TestWithTmpDir() {
    std::filesystem::path fs_path{tmp_dir() / "file.fs"};

    // Create empty file
    {
      std::ofstream tmpfile(fs_path, std::ios::binary | std::ios::out);
      tmpfile.exceptions(std::ofstream::failbit | std::ofstream::badbit);
    }

    // Resize it to the target size
    std::filesystem::resize_file(fs_path, FS_SIZE_IN_BYTES);

    // By default a mounted loop device is owned by root and can't be written by
    // a normal user. However there is the option -d to mkfs.ext4 that takes a
    // root directory to pre-populate the new filesystem with. When that is used
    // it will also use the permissions of that directory. This might be ext4
    // specific and would need a different solution for other file systems.
    //
    // Solution comes from: https://unix.stackexchange.com/q/727131/456
    std::filesystem::create_directories(tmp_dir() / "root-dir" / "mount");

    // Format as ext4
    exec(fmt::format("mkfs.ext4 -d {} {}", tmp_dir() / "root-dir",
                     fs_path.string()));

    // Create a loop device
    const auto loop_out = exec(fmt::format(
        "udisksctl loop-setup -f {} --no-user-interaction", fs_path.string()));
    std::regex re_loop("^.* (.+)\\.\n$");
    std::smatch smatch;
    if (std::regex_match(loop_out, smatch, re_loop)) {
      m_loop_device = smatch[1].str();
    } else {
      throw std::runtime_error("Could not parse loop device");
    }

    // Mount it
    const auto mount_out =
        exec(fmt::format("udisksctl mount --options rw -b {}", m_loop_device));
    std::regex re_mount(".* at (.*)\n");
    if (std::regex_match(mount_out, smatch, re_mount)) {
      m_mount_dir = smatch[1].str();
    } else {
      throw std::runtime_error("Could not parse mount dir");
    }
  }

  ~TestWithFilesystem() override {
    if (!m_loop_device.empty()) {
      try {
        exec(fmt::format("udisksctl unmount -b {} --no-user-interaction",
                         m_loop_device));
      } catch (const std::exception& ex) {
        std::cerr << "WARNING: Could not unmount loop device : " << ex.what()
                  << "\n";
      }
      try {
        exec(fmt::format("udisksctl loop-delete -b {} --no-user-interaction",
                         m_loop_device));
      } catch (const std::exception& ex) {
        std::cerr << "WARNING: Could not delete loop device : " << ex.what()
                  << "\n";
      }
    }
  }

  /** The directory with the mounted filesystem */
  [[nodiscard]] auto mount_dir() const { return m_mount_dir / "mount"; }

  /** How many bytes remaining of the created filesystem */
  [[nodiscard]] auto available() const {
    return std::filesystem::space(m_mount_dir).available;
  }

 private:
  std::filesystem::path m_mount_dir{};
  std::string m_loop_device{};
};

#endif  // !WIN32
