// -*- mode:c++; c-basic-offset : 2; - * -
#include "gtest/gtest.h"
#include "torrent.h"

TEST(torrent, a) {
  std::filesystem::path p(__FILE__);
  zit::Torrent t(p.parent_path() /= "test.torrent");
}
