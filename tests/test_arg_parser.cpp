#include "arg_parser.hpp"
#include "gtest/gtest.h"

#include <limits.h>
#include <stdexcept>

using namespace zit;

TEST(arg_parser, duplicate) {
  ArgParser parser("desc");
  bool dst{false};
  parser.add_option("--test", {}, "test help", dst);
  EXPECT_THROW(parser.add_option<bool>("--test", {}, "test help", dst),
               std::runtime_error);
}

TEST(arg_parser, bool) {
  {
    ArgParser parser("desc");
    bool dst = true;
    parser.add_option("--test", {}, "test help", dst);
    EXPECT_TRUE(dst);
  }

  {
    ArgParser parser("desc");
    bool dst = true;
    parser.add_option("--test", {false}, "test help", dst);
    EXPECT_FALSE(dst);
  }

  {
    ArgParser parser("desc");
    bool dst = false;
    parser.add_option("--test", {false}, "test help", dst);
    parser.parse({"cmd", "--test"});
    EXPECT_TRUE(dst);
  }
}

TEST(arg_parser, int) {
  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option("--test", {}, "test help", dst);
    EXPECT_EQ(dst, 1);
  }

  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option("--test", {2}, "test help", dst);
    EXPECT_EQ(dst, 2);
  }

  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option("--test", {2}, "test help", dst);
    parser.parse({"cmd", "--test", "3"});
    EXPECT_EQ(dst, 3);
  }

  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option("--test", {2}, "test help", dst);
    parser.parse({"cmd", "--test", "-3"});
    EXPECT_EQ(dst, -3);
  }
}

TEST(arg_parser, unsigned) {
  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option("--test", {}, "test help", dst);
    EXPECT_EQ(dst, 1);
  }

  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option("--test", {2}, "test help", dst);
    EXPECT_EQ(dst, 2);
  }

  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option("--test", {2}, "test help", dst);
    parser.parse({"cmd", "--test", "3"});
    EXPECT_EQ(dst, 3);
  }

  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option("--test", {2}, "test help", dst);
    EXPECT_THROW(parser.parse({"cmd", "--test", "-3"}), std::out_of_range);
  }
}

TEST(arg_parser, float) {
  {
    ArgParser parser("desc");
    float dst = 1.1F;
    parser.add_option("--test", {}, "test help", dst);
    EXPECT_EQ(dst, 1.1F);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1F;
    parser.add_option("--test", {2.2F}, "test help", dst);
    EXPECT_EQ(dst, 2.2F);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1F;
    parser.add_option("--test", {2.0F}, "test help", dst);
    parser.parse({"cmd", "--test", "3.3"});
    EXPECT_EQ(dst, 3.3F);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1F;
    parser.add_option("--test", {}, "test help", dst);
    parser.parse({"cmd", "--test", "3.14159"});
    EXPECT_EQ(dst, 3.14159F);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1F;
    parser.add_option("--test", {2.0F}, "test help", dst);
    EXPECT_THROW(parser.parse({"cmd", "--test", "1E39"}), std::out_of_range);
  }
}

TEST(arg_parser, string) {
  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option("--test", {}, "test help", dst);
    EXPECT_EQ(dst, "s");
  }

  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option("--test", {"t"}, "test help", dst);
    EXPECT_EQ(dst, "t");
  }

  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option("--test", {"t"}, "test help", dst);
    parser.parse({"cmd", "--test", "uj"});
    EXPECT_EQ(dst, "uj");
  }
}

TEST(arg_parser, required) {
  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option("--test", {"t"}, "test help", dst, true);
    EXPECT_THROW(parser.parse({"cmd"}), std::runtime_error);
  }
  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option("--test", {"t"}, "test help", dst, false);
    parser.parse({"cmd", "--test", "uj"});
    EXPECT_EQ(dst, "uj");
  }
}

TEST(arg_parser, help) {
  ArgParser parser("desc");
  EXPECT_EQ(parser.usage(), "Usage:\n\ndesc\n\n");
  int val{};
  int val2{};
  parser.add_option("--test", {}, "test help", val);
  EXPECT_EQ(parser.usage(), R"(Usage:

desc

  --test    test help 
)");
  parser.add_option("--req", {}, "test req", val2, true);
  EXPECT_EQ(parser.usage(), R"(Usage:

desc

  --test    test help 
  --req     test req (required)
)");
}

TEST(arg_parser, help_option) {
  int val{};
  int val2{};
  bool help{false};
  auto getParser = [&] {
    ArgParser parser("desc");
    parser.add_option("--test", {}, "test help", val);
    parser.add_option("--req", {}, "test req", val2, true);
    parser.add_help_option("--help", "Print help", help);
    return parser;
  };

  // Ensure we cannot leave out the required arg
  {
    auto parser = getParser();
    EXPECT_THROW(parser.parse({"cmd"}), std::runtime_error);
  }

  // But still use help only
  {
    auto parser = getParser();
    parser.parse({"cmd", "--help"});
  }
}

TEST(arg_parser, duplicate_dst) {
  ArgParser parser("desc");
  int val{false};
  parser.add_option("--test", {}, "test help", val);
  // Same dst should throw
  EXPECT_THROW(parser.add_option("--test2", {}, "test help", val),
               std::runtime_error);
}

TEST(arg_parser, alias) {
  int val{false};
  auto getParser = [&] {
    ArgParser parser("desc");
    parser.add_option("--test,-t", {}, "test help", val);
    return parser;
  };

  {
    getParser().parse({"cmd", "--test", "1"});
    EXPECT_EQ(val, 1);
  }

  {
    getParser().parse({"cmd", "-t", "2"});
    EXPECT_EQ(val, 2);
  }
}

TEST(arg_parser, alias_help) {
  bool val{false};
  auto getParser = [&] {
    ArgParser parser("desc");
    parser.add_help_option("--help,-h", "test help", val);
    return parser;
  };

  {
    getParser().parse({"cmd", "--help"});
    EXPECT_TRUE(val);
  }

  {
    getParser().parse({"cmd", "-h"});
    EXPECT_TRUE(val);
  }

  {
    getParser().parse({"cmd"});
    EXPECT_FALSE(val);
  }
}
