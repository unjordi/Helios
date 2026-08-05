/**
 * @file tests/unit/test_crypto.cpp
 * @brief Test src/crypto.*.
 */
// test imports
#include "../tests_common.h"

// lib imports
#include <openssl/x509.h>

// local imports
#include <src/crypto.h>

TEST(CryptoTest, GeneratedCredentialsExposeSubjectAndVerifySignatures) {
  constexpr std::string_view common_name = "Sunshine Test Host";
  constexpr std::string_view payload = "payload";

  auto creds = crypto::gen_creds(common_name, 2048);
  ASSERT_FALSE(creds.x509.empty());
  ASSERT_FALSE(creds.pkey.empty());

  auto cert = crypto::x509(creds.x509);
  auto pkey = crypto::pkey(creds.pkey);
  ASSERT_NE(cert.get(), nullptr);
  ASSERT_NE(pkey.get(), nullptr);

  const auto subject = X509_get_subject_name(cert.get());
  ASSERT_NE(subject, nullptr);

  const auto common_name_index = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
  ASSERT_GE(common_name_index, 0);

  const auto common_name_entry = X509_NAME_get_entry(subject, common_name_index);
  ASSERT_NE(common_name_entry, nullptr);

  const auto common_name_data = X509_NAME_ENTRY_get_data(common_name_entry);
  ASSERT_NE(common_name_data, nullptr);

  const std::string_view parsed_common_name {
    reinterpret_cast<const char *>(ASN1_STRING_get0_data(common_name_data)),
    static_cast<std::size_t>(ASN1_STRING_length(common_name_data))
  };
  ASSERT_EQ(parsed_common_name, common_name);

  ASSERT_FALSE(crypto::signature(cert).empty());

  const auto signature = crypto::sign256(pkey, payload);
  ASSERT_FALSE(signature.empty());
  ASSERT_TRUE(crypto::verify256(cert, payload, {reinterpret_cast<const char *>(signature.data()), signature.size()}));
}

namespace {
  constexpr bool has(crypto::PERM mask, crypto::PERM bit) {
    return static_cast<uint32_t>(mask) & static_cast<uint32_t>(bit);
  }
}  // namespace

/**
 * @brief A newly paired client must be able to stream without a manual permission edit.
 *
 * Pairing hands new clients PERM::_default (see nvhttp.cpp). When that mask lacked
 * `launch`, pairing reported success and every subsequent app launch was rejected with
 * an HTTP 403 that is only logged at debug level, so the failure looked like a network
 * or host problem rather than a missing permission.
 */
TEST(CryptoPermTest, DefaultPermissionsCanStreamAndControlASession) {
  constexpr auto perm = crypto::PERM::_default;

  EXPECT_TRUE(has(perm, crypto::PERM::list));
  EXPECT_TRUE(has(perm, crypto::PERM::view));
  EXPECT_TRUE(has(perm, crypto::PERM::launch));
  EXPECT_TRUE(has(perm, crypto::PERM::input_controller));
  EXPECT_TRUE(has(perm, crypto::PERM::input_mouse));
  EXPECT_TRUE(has(perm, crypto::PERM::input_kbd));
}

/**
 * @brief Defaults must stay confined to the stream itself.
 *
 * Anything that reaches into the host beyond streaming stays opt-in, so that widening
 * the defaults never silently grants a freshly paired client access to the clipboard,
 * the filesystem or arbitrary server commands.
 */
TEST(CryptoPermTest, DefaultPermissionsWithholdHostReachingCapabilities) {
  constexpr auto perm = crypto::PERM::_default;

  EXPECT_FALSE(has(perm, crypto::PERM::clipboard_set));
  EXPECT_FALSE(has(perm, crypto::PERM::clipboard_read));
  EXPECT_FALSE(has(perm, crypto::PERM::file_upload));
  EXPECT_FALSE(has(perm, crypto::PERM::file_dwnload));
  EXPECT_FALSE(has(perm, crypto::PERM::server_cmd));
  EXPECT_FALSE(has(perm, crypto::PERM::input_touch));
  EXPECT_FALSE(has(perm, crypto::PERM::input_pen));
}
