/**
 * @file tests/unit/test_utility_hex.cpp
 * @brief Test util::hex_vec / from_hex_vec / from_hex — the byte<->hex helpers
 *        that underpin the pairing crypto.
 */
#include "../tests_common.h"

#include <src/utility.h>

#include <array>
#include <cstdint>

TEST(UtilHex, HexVecForwardUppercase) {
  std::string in {"\x00\x0A\xFF", 3};
  ASSERT_EQ(util::hex_vec(in, true), "000AFF");  // rev=true => forward order
}

TEST(UtilHex, RoundTripForward) {
  std::string in {"\x01\x23\x45\x67\x89\xAB\xCD\xEF", 8};
  auto hex = util::hex_vec(in, true);
  ASSERT_EQ(util::from_hex_vec(hex, true), in);
}

TEST(UtilHex, FromHexArrayLength) {
  auto a = util::from_hex<std::array<std::uint8_t, 4>>("DEADBEEF", true);
  ASSERT_EQ(a[0], 0xDE);
  ASSERT_EQ(a[3], 0xEF);
}
