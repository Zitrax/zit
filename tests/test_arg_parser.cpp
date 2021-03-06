#include "arg_parser.h"
#include "gtest/gtest.h"

#include <limits.h>

using namespace zit;

TEST(arg_parser, duplicate) {
  ArgParser parser("desc");
  bool dst;
  parser.add_option<bool>("--test", {}, "test help", dst);
  EXPECT_THROW(parser.add_option<bool>("--test", {}, "test help", dst),
               std::runtime_error);
}

TEST(arg_parser, bool) {
  {
    ArgParser parser("desc");
    bool dst = true;
    parser.add_option<bool>("--test", {}, "test help", dst);
    EXPECT_TRUE(dst);
  }

  {
    ArgParser parser("desc");
    bool dst = true;
    parser.add_option<bool>("--test", false, "test help", dst);
    EXPECT_FALSE(dst);
  }

  {
    ArgParser parser("desc");
    bool dst = false;
    parser.add_option<bool>("--test", false, "test help", dst);
    const char* n_argv[] = {"cmd", "--test"};
    const char** argv = n_argv;
    parser.parse(2, argv);
    EXPECT_TRUE(dst);
  }
}

TEST(arg_parser, int) {
  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option<int>("--test", {}, "test help", dst);
    EXPECT_EQ(dst, 1);
  }

  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option<int>("--test", 2, "test help", dst);
    EXPECT_EQ(dst, 2);
  }

  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option<int>("--test", 2, "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "3"};
    const char** argv = n_argv;
    parser.parse(3, argv);
    EXPECT_EQ(dst, 3);
  }

  {
    ArgParser parser("desc");
    int dst = 1;
    parser.add_option<int>("--test", 2, "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "-3"};
    const char** argv = n_argv;
    parser.parse(3, argv);
    EXPECT_EQ(dst, -3);
  }
}

TEST(arg_parser, unsigned) {
  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option<unsigned>("--test", {}, "test help", dst);
    EXPECT_EQ(dst, 1);
  }

  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option<unsigned>("--test", 2, "test help", dst);
    EXPECT_EQ(dst, 2);
  }

  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option<unsigned>("--test", 2, "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "3"};
    const char** argv = n_argv;
    parser.parse(3, argv);
    EXPECT_EQ(dst, 3);
  }

  {
    ArgParser parser("desc");
    unsigned dst = 1;
    parser.add_option<unsigned>("--test", 2, "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "-3"};
    const char** argv = n_argv;
    EXPECT_THROW(parser.parse(3, argv), std::out_of_range);
  }
}

TEST(arg_parser, float) {
  {
    ArgParser parser("desc");
    float dst = 1.1f;
    parser.add_option<float>("--test", {}, "test help", dst);
    EXPECT_EQ(dst, 1.1f);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1f;
    parser.add_option<float>("--test", 2.2f, "test help", dst);
    EXPECT_EQ(dst, 2.2f);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1f;
    parser.add_option<float>("--test", 2.0f, "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "3.3"};
    const char** argv = n_argv;
    parser.parse(3, argv);
    EXPECT_EQ(dst, 3.3f);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1f;
    parser.add_option<float>("--test", {}, "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "3.14159"};
    const char** argv = n_argv;
    parser.parse(3, argv);
    EXPECT_EQ(dst, 3.14159f);
  }

  {
    ArgParser parser("desc");
    float dst = 1.1f;
    parser.add_option<float>("--test", 2.0f, "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "1E39"};
    const char** argv = n_argv;
    EXPECT_THROW(parser.parse(3, argv), std::out_of_range);
  }
}

TEST(arg_parser, string) {
  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option<std::string>("--test", {}, "test help", dst);
    EXPECT_EQ(dst, "s");
  }

  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option<std::string>("--test", "t", "test help", dst);
    EXPECT_EQ(dst, "t");
  }

  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option<std::string>("--test", "t", "test help", dst);
    const char* n_argv[] = {"cmd", "--test", "uj"};
    const char** argv = n_argv;
    parser.parse(3, argv);
    EXPECT_EQ(dst, "uj");
  }
}

TEST(arg_parser, required) {
  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option<std::string>("--test", "t", "test help", dst, true);
    const char* n_argv[] = {"cmd"};
    const char** argv = n_argv;
    EXPECT_THROW(parser.parse(1, argv), std::runtime_error);
  }
  {
    ArgParser parser("desc");
    std::string dst = "s";
    parser.add_option<std::string>("--test", "t", "test help", dst, false);
    const char* n_argv[] = {"cmd", "--test", "uj"};
    const char** argv = n_argv;
    parser.parse(3, argv);
    EXPECT_EQ(dst, "uj");
  }
}

TEST(arg_parser, help) {
  ArgParser parser("desc");
  EXPECT_EQ(parser.usage(), "Usage:\n\ndesc\n\n");
  int val;
  parser.add_option("--test", {}, "test help", val);
  EXPECT_EQ(parser.usage(), R"(Usage:

desc

  --test    test help 
)");
  parser.add_option("--req", {}, "test req", val, true);
  EXPECT_EQ(parser.usage(), R"(Usage:

desc

  --test    test help 
  --req     test req (required)
)");
}
