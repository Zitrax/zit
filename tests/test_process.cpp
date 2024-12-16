#include "gtest/gtest.h"

#include "process.hpp"
#include "test_utils.hpp"

#include <chrono>

using namespace std::chrono_literals;

const auto ls_command = []() -> std::vector<std::string> {
#ifdef __linux__
  return {"ls"};
#else
  return {"cmd.exe", "/C", "dir"};
#endif
}();

const auto sleep_command = []() -> std::vector<std::string> {
#ifdef __linux__
  return {"sleep", "1"};
#else
  // Windows timeout is weird, it does not seem to wait exactly N seconds
  // instead I am guessing it waits for the next N seconds boundary. I.e.
  // timeout 1 might wait for anything between 0 and 1 seconds, thus we
  // use 2 seconds to be sure to wait more than 1 second.
  return {"timeout", "/NOBREAK", "/T", "2"};
#endif
}();

const auto touch_command = []() -> std::vector<std::string> {
#ifdef __linux__
  return {"touch", "test_file.txt"};
#else
  return {"cmd.exe", "/C", "copy", "NUL", "test_file.txt"};
#endif
}();

const auto rm_command = []() -> std::vector<std::string> {
#ifdef __linux__
  return {"rm", "test_file.txt"};
#else
  return {"cmd.exe", "/C", "del", "test_file.txt"};
#endif
}();

TEST(Process, basic) {
  EXPECT_NO_THROW(zit::Process process("ls", {ls_command}, nullptr, {},
                                       zit::Process::Mode::FOREGROUND));
}

TEST(Process, basic_background) {
  // This simple command should throw, since it will immediately finish
  // and already be dead in the constructor check.
  EXPECT_THROW(zit::Process process("ls", {ls_command}, nullptr, {},
                                    zit::Process::Mode::BACKGROUND),
               std::runtime_error);
}

TEST(Process, basic_background_sleep) {
  const auto start = std::chrono::steady_clock::now();
  zit::Process process("sleep", sleep_command, nullptr, {},
                       zit::Process::Mode::BACKGROUND);
  EXPECT_TRUE(process.wait_for_exit(2s));
  EXPECT_GE(std::chrono::steady_clock::now() - start, 1s);
}

using ProcessF = TestWithTmpDir;

TEST_F(ProcessF, stop_cmd) {
  {
    std::string tmp_dir_str = tmp_dir().string();
    zit::Process process("touch", touch_command, tmp_dir_str.c_str(),
                         rm_command, zit::Process::Mode::FOREGROUND);
    EXPECT_TRUE(process.wait_for_exit(500ms));
    EXPECT_TRUE(std::filesystem::exists(tmp_dir() / "test_file.txt"));
  }
  EXPECT_FALSE(std::filesystem::exists(tmp_dir() / "test_file.txt"));
}
