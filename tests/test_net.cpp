// -*- mode:c++; c-basic-offset : 2; - * -
#include "gtest/gtest.h"
#include "net.h"

using namespace zit;
using namespace std;

TEST(net, url_encode) {
  string input =
      "\x12\x34\x56\x78\x9a\xbc\xde\xf1\x23\x45"
      "\x67\x89\xab\xcd\xef\x12\x34\x56\x78\x9a";
  EXPECT_EQ(Net::url_encode(input),
            "%124Vx%9A%BC%DE%F1%23Eg%89%AB%CD%EF%124Vx%9A");
}
