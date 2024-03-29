#include "arg_parser.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <limits.h>
#include <stdexcept>

using namespace zit;

using ::testing::ElementsAreArray;

TEST(arg_parser, duplicate) {
  ArgParser parser("desc");
  parser.add_option<bool>("--test");
  EXPECT_THROW(parser.add_option<bool>("--test"), std::runtime_error);
}

TEST(arg_parser, no_such_option) {
  ArgParser parser("desc");
  parser.add_option<bool>("--test");
  EXPECT_THROW(std::ignore = parser.get<bool>("--test2"), std::runtime_error);
}

TEST(arg_parser, bool) {
  {
    ArgParser parser("desc");
    parser.add_option<bool>("--test");
    // Bool implicitly defaults to false
    EXPECT_FALSE(parser.get<bool>("--test"));
  }

  {
    ArgParser parser("desc");
    parser.add_option<bool>("--test").default_value(true);
    EXPECT_TRUE(parser.get<bool>("--test"));
  }

  {
    ArgParser parser("desc");
    parser.add_option<bool>("--test");
    parser.parse({"cmd", "--test"});
    EXPECT_TRUE(parser.get<bool>("--test"));
    EXPECT_THROW(std::ignore = parser.get<int>("--test"), std::runtime_error);
  }
}

TEST(arg_parser, int) {
  {
    ArgParser parser("desc");
    parser.add_option<int>("--test");
    EXPECT_FALSE(parser.is_provided("--test"));
    EXPECT_THROW(std::ignore = parser.get<int>("--test"), std::runtime_error);
  }

  {
    ArgParser parser("desc");
    parser.add_option<int>("--test").default_value(2);
    EXPECT_FALSE(parser.is_provided("--test"));
    EXPECT_THROW(std::ignore = parser.get_multi<int>("--test"),
                 std::runtime_error);
    EXPECT_EQ(parser.get<int>("--test"), 2);
  }

  {
    ArgParser parser("desc");
    parser.add_option<int>("--test").default_value(2);
    parser.parse({"cmd", "--test", "3"});
    EXPECT_TRUE(parser.is_provided("--test"));
    EXPECT_EQ(parser.get<int>("--test"), 3);
  }

  {
    ArgParser parser("desc");
    parser.add_option<int>("--test");
    parser.parse({"cmd", "--test", "-3"});
    EXPECT_EQ(parser.get<int>("--test"), -3);
  }

  {
    ArgParser parser("desc");
    parser.add_option<int>("--test");
    EXPECT_THROW(parser.parse({"cmd", "--test", "-3", "--test", "4"}),
                 std::runtime_error);
    EXPECT_EQ(parser.get<int>("--test"), -3);
  }
}

TEST(arg_parser, positional) {
  {  // Single positional arg
    ArgParser parser("desc");
    parser.add_option<int>("--test").positional(0);
    EXPECT_FALSE(parser.is_provided("--test"));
    parser.parse({"cmd", "5"});
    EXPECT_TRUE(parser.is_provided("--test"));
    EXPECT_EQ(parser.get<int>("--test"), 5);
  }

  {  // Single provided required positional arg
    ArgParser parser("desc");
    parser.add_option<int>("--test").positional(0).required();
    EXPECT_FALSE(parser.is_provided("--test"));
    parser.parse({"cmd", "5"});
    EXPECT_TRUE(parser.is_provided("--test"));
    EXPECT_EQ(parser.get<int>("--test"), 5);
  }

  {  // Single non provided required positional arg
    ArgParser parser("desc");
    parser.add_option<int>("--test").positional(0).required();
    EXPECT_FALSE(parser.is_provided("--test"));
    EXPECT_THROW(parser.parse({"cmd"}), std::runtime_error);
  }

  {  // Dual positional args
    ArgParser parser("desc");
    parser.add_option<int>("--test").positional(0);
    parser.add_option<int>("--test2").positional(1);
    EXPECT_FALSE(parser.is_provided("--test"));
    EXPECT_FALSE(parser.is_provided("--test2"));
    parser.parse({"cmd", "5", "6"});
    EXPECT_TRUE(parser.is_provided("--test"));
    EXPECT_TRUE(parser.is_provided("--test2"));
    EXPECT_EQ(parser.get<int>("--test"), 5);
    EXPECT_EQ(parser.get<int>("--test2"), 6);
  }

  {  // Duplicate positional args
    ArgParser parser("desc");
    parser.add_option<int>("--test").positional(0);
    parser.add_option<int>("--test2").positional(0);
    EXPECT_FALSE(parser.is_provided("--test"));
    EXPECT_FALSE(parser.is_provided("--test2"));
    EXPECT_THROW(parser.parse({"cmd", "5", "6"}), std::runtime_error);
  }

  {  // Mixed named and positional args
    ArgParser parser("desc");
    parser.add_option<int>("--test").positional(0);
    parser.add_option<int>("--test2").positional(1);
    parser.add_option<int>("--named");
    parser.add_option<int>("--named2");
    EXPECT_FALSE(parser.is_provided("--test"));
    EXPECT_FALSE(parser.is_provided("--test2"));
    EXPECT_FALSE(parser.is_provided("--named"));
    EXPECT_FALSE(parser.is_provided("--named2"));
    parser.parse({"cmd", "5", "--named", "6", "7", "--named2", "8"});
    EXPECT_TRUE(parser.is_provided("--test"));
    EXPECT_TRUE(parser.is_provided("--test2"));
    EXPECT_TRUE(parser.is_provided("--named"));
    EXPECT_TRUE(parser.is_provided("--named2"));

    EXPECT_EQ(parser.get<int>("--test"), 5);
    EXPECT_EQ(parser.get<int>("--test2"), 7);
    EXPECT_EQ(parser.get<int>("--named"), 6);
    EXPECT_EQ(parser.get<int>("--named2"), 8);
  }
}

TEST(arg_parser, collecting) {
  {  // Single collecting
    ArgParser parser("desc");
    auto& option = parser.add_option<int>("--rem").collecting();
    EXPECT_EQ(option.get_type(), ArgParser::Type::INT);
    EXPECT_TRUE(option.is_multi());
    EXPECT_TRUE(option.is_collecting());
    EXPECT_FALSE(parser.is_provided("--rem"));
    parser.parse({"cmd", "5"});
    EXPECT_TRUE(parser.is_provided("--rem"));
    EXPECT_THROW(std::ignore = parser.get<int>("--rem"), std::runtime_error);
    EXPECT_THAT(parser.get_multi<int>("--rem"), ElementsAreArray({5}));
  }

  {  // Dual collecting
    ArgParser parser("desc");
    auto& option = parser.add_option<int>("--rem").collecting();
    EXPECT_EQ(option.get_type(), ArgParser::Type::INT);
    EXPECT_TRUE(option.is_multi());
    EXPECT_TRUE(option.is_collecting());
    EXPECT_FALSE(parser.is_provided("--rem"));
    parser.parse({"cmd", "5", "6"});
    EXPECT_TRUE(parser.is_provided("--rem"));
    EXPECT_THROW(std::ignore = parser.get<int>("--rem"), std::runtime_error);
    EXPECT_THAT(parser.get_multi<int>("--rem"), ElementsAreArray({5, 6}));
  }

  {  // Mixed
    ArgParser parser("desc");
    parser.add_option<float>("--rem").collecting();
    parser.add_option<float>("--named");
    parser.add_option<float>("--multi").multi();
    parser.parse({"cmd", "5.0", "--named", "-1.1", "--multi", "2.0", "--multi",
                  "2.1", "5.1"});
    EXPECT_THAT(parser.get_multi<float>("--rem"), ElementsAreArray({5.0, 5.1}));
  }

  {  // Required collecting
    ArgParser parser("desc");
    parser.add_option<float>("--rem").collecting().required();
    EXPECT_THROW(parser.parse({"cmd"}), std::runtime_error);
  }

  {  // Zero collecting
    ArgParser parser("desc");
    parser.add_option<float>("--rem").collecting();
    parser.parse({"cmd"});
    EXPECT_TRUE(parser.get_multi<float>("--rem").empty());
  }

  {  // Two different collecting
    ArgParser parser("desc");
    parser.add_option<float>("--rem").collecting();
    parser.add_option<float>("--rem2").collecting();
    EXPECT_THROW(parser.parse({"cmd"}), std::runtime_error);
  }

  {  // Collecting with default
    ArgParser parser("desc");
    parser.add_option<unsigned>("--rem").collecting().default_value(3U);
    parser.parse({"cmd"});
    EXPECT_THAT(parser.get_multi<unsigned>("--rem"), ElementsAreArray({3U}));
  }
}

TEST(arg_parser, int_multi) {
  {
    ArgParser parser("desc");
    auto& option = parser.add_option<int>("--test").multi();
    EXPECT_EQ(option.get_type(), ArgParser::Type::INT);
    parser.parse({"cmd", "--test", "-3"});
    EXPECT_THROW(std::ignore = parser.get<int>("--test"), std::runtime_error);
    EXPECT_THAT(parser.get_multi<int>("--test"), ElementsAreArray({-3}));
  }

  {
    ArgParser parser("desc");
    auto& option = parser.add_option<int>("--test").multi();
    EXPECT_EQ(option.get_type(), ArgParser::Type::INT);
    parser.parse({"cmd", "--test", "-3", "--test", "4"});
    EXPECT_THROW(std::ignore = parser.get<int>("--test"), std::runtime_error);
    EXPECT_THAT(parser.get_multi<int>("--test"), ElementsAreArray({-3, 4}));
  }

  {
    ArgParser parser("desc");
    auto& option =
        parser.add_option<int>("--test").multi().default_value({1, 2, 3});
    EXPECT_EQ(option.get_type(), ArgParser::Type::INT);
    parser.parse({"cmd"});
    EXPECT_THROW(std::ignore = parser.get<int>("--test"), std::runtime_error);
    EXPECT_THAT(parser.get_multi<int>("--test"), ElementsAreArray({1, 2, 3}));
  }
}

TEST(arg_parser, unsigned) {
  {
    ArgParser parser("desc");
    parser.add_option<unsigned>("--test");
    EXPECT_THROW(std::ignore = parser.get<unsigned>("--test"),
                 std::runtime_error);
  }

  {
    ArgParser parser("desc");
    parser.add_option<unsigned>("--test").default_value(2);
    EXPECT_EQ(parser.get<unsigned>("--test"), 2);
    EXPECT_THROW(std::ignore = parser.get<int>("--test"), std::runtime_error);
  }

  {
    ArgParser parser("desc");
    parser.add_option<unsigned>("--test").default_value(2);
    parser.parse({"cmd", "--test", "3"});
    EXPECT_EQ(parser.get<unsigned>("--test"), 3);
  }

  {
    ArgParser parser("desc");
    parser.add_option<unsigned>("--test").default_value(2);
    EXPECT_THROW(parser.parse({"cmd", "--test", "-3"}), std::out_of_range);
  }
}

TEST(arg_parser, float) {
  {
    ArgParser parser("desc");
    parser.add_option<float>("--test");
    EXPECT_THROW(std::ignore = parser.get<float>("--test"), std::runtime_error);
  }

  {
    ArgParser parser("desc");
    parser.add_option<float>("--test").default_value(2.2F);
    EXPECT_EQ(parser.get<float>("--test"), 2.2F);
  }

  {
    ArgParser parser("desc");
    parser.add_option<float>("--test").default_value(2.0F);
    parser.parse({"cmd", "--test", "3.3"});
    EXPECT_EQ(parser.get<float>("--test"), 3.3F);
  }

  {
    ArgParser parser("desc");
    parser.add_option<float>("--test");
    parser.parse({"cmd", "--test", "3.14159"});
    EXPECT_EQ(parser.get<float>("--test"), 3.14159F);
  }

  {
    ArgParser parser("desc");
    parser.add_option<float>("--test").default_value(2.0F);
    EXPECT_THROW(parser.parse({"cmd", "--test", "1E39"}), std::out_of_range);
  }
}

TEST(arg_parser, string) {
  {
    ArgParser parser("desc");
    parser.add_option<std::string>("--test");
    EXPECT_THROW(std::ignore = parser.get<std::string>("--test"),
                 std::runtime_error);
  }

  {
    ArgParser parser("desc");
    parser.add_option<std::string>("--test").default_value("t");
    EXPECT_EQ(parser.get<std::string>("--test"), "t");
  }

  {
    ArgParser parser("desc");
    parser.add_option<std::string>("--test").default_value("t");
    parser.parse({"cmd", "--test", "uj"});
    EXPECT_EQ(parser.get<std::string>("--test"), "uj");
  }
}

TEST(arg_parser, required) {
  {
    ArgParser parser("desc");
    parser.add_option<std::string>("--test").default_value("t").required();
    EXPECT_THROW(parser.parse({"cmd"}), std::runtime_error);
  }
  {
    ArgParser parser("desc");
    parser.add_option<std::string>("--test").default_value("t");
    parser.parse({"cmd", "--test", "uj"});
    EXPECT_EQ(parser.get<std::string>("--test"), "uj");
  }
}

TEST(arg_parser, help) {
  ArgParser parser("desc");
  EXPECT_EQ(parser.usage(), "Usage:\n\ndesc\n\n");
  parser.add_option<int>("--test").help("test help");
  EXPECT_EQ(parser.usage(), R"(Usage:

desc

  --test    test help 
)");
  parser.add_option<int>("--req").help("test req").required();
  EXPECT_EQ(parser.usage(), R"(Usage:

desc

  --test    test help 
  --req     test req (required)
)");
}

TEST(arg_parser, help_option) {
  auto getParser = [&] {
    ArgParser parser("desc");
    parser.add_option<int>("--test").help("test help");
    parser.add_option<int>("--req").help("test req").required();
    parser.add_option<bool>("--help").help("Print help").help_arg();
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

TEST(arg_parser, alias) {
  auto getParser = [&] {
    ArgParser parser("desc");
    parser.add_option<int>("--test").aliases({"-t"});
    return parser;
  };

  {
    auto parser = getParser();
    parser.parse({"cmd", "--test", "1"});
    EXPECT_EQ(parser.get<int>("--test"), 1);
    EXPECT_EQ(parser.get<int>("-t"), 1);
  }

  {
    auto parser = getParser();
    parser.parse({"cmd", "-t", "2"});
    EXPECT_EQ(parser.get<int>("--test"), 2);
    EXPECT_EQ(parser.get<int>("-t"), 2);
  }
}

TEST(arg_parser, alias_help) {
  auto getParser = [&] {
    ArgParser parser("desc");
    parser.add_option<bool>("--help").aliases({"-h"}).help_arg();
    return parser;
  };

  {
    auto parser = getParser();
    parser.parse({"cmd", "--help"});
    EXPECT_TRUE(parser.get<bool>("--help"));
  }

  {
    auto parser = getParser();
    parser.parse({"cmd", "-h"});
    EXPECT_TRUE(parser.get<bool>("-h"));
  }

  {
    auto parser = getParser();
    parser.parse({"cmd", "-h"});
    EXPECT_TRUE(parser.get<bool>("--help"));
  }
}

namespace {
void test_help_required(bool multi) {
  auto getParser = [&] {
    ArgParser parser("desc");
    parser.add_option<bool>("--help").aliases({"-h", "/?"}).help_arg();
    auto& test_arg = parser.add_option<std::string>("--test").required();
    if (multi) {
      test_arg.multi();
    }
    return parser;
  };

  {
    auto parser = getParser();
    parser.parse({"cmd", "--help"});
    EXPECT_TRUE(parser.get<bool>("--help"));
  }

  {
    auto parser = getParser();
    parser.parse({"cmd", "-h"});
    EXPECT_TRUE(parser.get<bool>("-h"));
  }

  {
    // Missing required arg
    EXPECT_THROW(getParser().parse({"cmd"}), std::runtime_error);
  }

  {
    auto parser = getParser();
    parser.parse({"cmd", "-h", "--test", "s"});
    EXPECT_TRUE(parser.get<bool>("-h"));
    if (multi) {
      EXPECT_THAT(parser.get_multi<std::string>("--test"),
                  ElementsAreArray({"s"}));
    } else {
      EXPECT_EQ(parser.get<std::string>("--test"), "s");
    }
  }
}
}  // namespace

TEST(arg_parser, help_plus_required) {
  test_help_required(false);
}

TEST(arg_parser, help_plus_required_multi) {
  test_help_required(true);
}
