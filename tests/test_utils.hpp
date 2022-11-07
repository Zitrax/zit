// -*- mode:c++; c-basic-offset : 2; -*-
#pragma once

#include <filesystem>

#include "gtest/gtest.h"

class TestWithTmpDir : public ::testing::Test {
 public:
  TestWithTmpDir() {
    if (!mkdtemp(m_dirname.data())) {
      throw std::runtime_error("Could not create temporary directory");
    }
    m_created = true;
  }

  ~TestWithTmpDir() override {
    if (m_created) {
      std::filesystem::remove_all(m_dirname.data());
    }
  }

  /**
   * The temporary directory when running the test.
   */
  [[nodiscard]] std::filesystem::path tmp_dir() const {
    return m_dirname.data();
  }

 private:
  bool m_created = false;
  std::array<char, 16> m_dirname{'/', 't', 'm', 'p', '/', 'z', 'i', 't',
                                 '_', 'X', 'X', 'X', 'X', 'X', 'X', '\0'};
};
