/**
 * @file tests/unit/test_config_parse.cpp
 * @brief Test config::parse_config — pure config parsing.
 */
#include "../tests_common.h"

#include <src/config.h>

using config::parse_config;

TEST(ConfigParse, BasicKeyValue) {
  auto v = parse_config("port = 47989\n");
  ASSERT_EQ(v.size(), 1u);
  ASSERT_EQ(v.at("port"), "47989");
}

TEST(ConfigParse, IgnoresFullLineComment) {
  auto v = parse_config("# only a comment\nname = bob\n");
  ASSERT_EQ(v.size(), 1u);
  ASSERT_EQ(v.at("name"), "bob");
}

TEST(ConfigParse, IgnoresLinesWithoutEquals) {
  auto v = parse_config("not a setting\nreal = ok\n");
  ASSERT_EQ(v.size(), 1u);
  ASSERT_EQ(v.at("real"), "ok");
}

TEST(ConfigParse, MultilineListValueIsCaptured) {
  auto v = parse_config("modes = [\n  1920x1080,\n  1280x720\n]\n");
  ASSERT_EQ(v.count("modes"), 1u);
  EXPECT_NE(v.at("modes").find("1920x1080"), std::string::npos);
  EXPECT_NE(v.at("modes").find("1280x720"), std::string::npos);
}

TEST(ConfigParse, HandlesCrlfLineEndings) {
  auto v = parse_config("a = 1\r\nb = 2\r\n");
  ASSERT_EQ(v.at("a"), "1");
  ASSERT_EQ(v.at("b"), "2");
}
