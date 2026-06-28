/**
 * @file tests/unit/test_security_cert_validation.cpp
 * @brief Security regression tests for client-certificate validation.
 *
 * Encodes the FIXED behavior for the authentication-bypass reported as
 * GHSA-ph75-mgxh-mv57 / CVE-2026-32253: crypto::cert_chain_t::verify() must
 * REJECT client certificates that are expired or not-yet-valid. The current
 * openssl_verify_cb (src/crypto.cpp) returns 1 for X509_V_ERR_CERT_HAS_EXPIRED
 * and X509_V_ERR_CERT_NOT_YET_VALID, so those two tests are EXPECTED TO FAIL
 * until the real upstream fix is ported. They are kept red on purpose to hold
 * the bar high and prevent the bypass from being forgotten.
 */
#include "../tests_common.h"

#include <src/crypto.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace {

  // Generate a self-signed RSA cert as a PEM string, with notBefore/notAfter
  // offset (in seconds) from now. Negative notAfter => already expired;
  // positive notBefore => not yet valid.
  std::string make_self_signed_pem(long not_before_secs, long not_after_secs) {
    EVP_PKEY *pkey = EVP_RSA_gen(2048);
    X509 *x = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
    X509_gmtime_adj(X509_getm_notBefore(x), not_before_secs);
    X509_gmtime_adj(X509_getm_notAfter(x), not_after_secs);
    X509_set_pubkey(x, pkey);

    X509_NAME *name = X509_get_subject_name(x);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *) "test-client", -1, -1, 0);
    X509_set_issuer_name(x, name);  // self-signed
    X509_sign(x, pkey, EVP_sha256());

    BIO *bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, x);
    BUF_MEM *mem = nullptr;
    BIO_get_mem_ptr(bio, &mem);
    std::string pem(mem->data, mem->length);

    BIO_free(bio);
    X509_free(x);
    EVP_PKEY_free(pkey);
    return pem;
  }

  crypto::p_named_cert_t make_named(const std::string &pem) {
    auto nc = std::make_shared<crypto::named_cert_t>();
    nc->name = "test-client";
    nc->uuid = "00000000-0000-0000-0000-000000000000";
    nc->cert = pem;
    nc->perm = crypto::PERM::_all;
    return nc;
  }

}  // namespace

// Sanity: a currently-valid client cert is accepted. Should PASS today.
TEST(SecurityCertValidation, AcceptsValidClientCert) {
  auto pem = make_self_signed_pem(-3600, 3600 * 24 * 365);  // valid since 1h ago, 1y left
  auto nc = make_named(pem);
  crypto::cert_chain_t chain;
  chain.add(nc);

  auto x = crypto::x509(pem);
  ASSERT_TRUE(x.get() != nullptr);
  crypto::p_named_cert_t out;
  ASSERT_EQ(chain.verify(x.get(), out), nullptr) << "a valid client cert must be accepted";
}

// CVE-2026-32253: expired client certs must be REJECTED. EXPECTED TO FAIL today
// (openssl_verify_cb returns 1 for X509_V_ERR_CERT_HAS_EXPIRED).
TEST(SecurityCertValidation, RejectsExpiredClientCert) {
  auto pem = make_self_signed_pem(-3600 * 48, -3600 * 24);  // expired ~1 day ago
  auto nc = make_named(pem);
  crypto::cert_chain_t chain;
  chain.add(nc);

  auto x = crypto::x509(pem);
  crypto::p_named_cert_t out;
  ASSERT_NE(chain.verify(x.get(), out), nullptr)
    << "expired client cert must be rejected (GHSA-ph75-mgxh-mv57 / CVE-2026-32253)";
}

// CVE-2026-32253: not-yet-valid client certs must be REJECTED. EXPECTED TO FAIL
// today (openssl_verify_cb returns 1 for X509_V_ERR_CERT_NOT_YET_VALID).
TEST(SecurityCertValidation, RejectsNotYetValidClientCert) {
  auto pem = make_self_signed_pem(3600 * 24, 3600 * 48);  // becomes valid tomorrow
  auto nc = make_named(pem);
  crypto::cert_chain_t chain;
  chain.add(nc);

  auto x = crypto::x509(pem);
  crypto::p_named_cert_t out;
  ASSERT_NE(chain.verify(x.get(), out), nullptr)
    << "not-yet-valid client cert must be rejected (GHSA-ph75-mgxh-mv57 / CVE-2026-32253)";
}

// An unknown client cert (not in the store) must be rejected. Should PASS today.
TEST(SecurityCertValidation, RejectsUnknownClientCert) {
  auto trusted = make_self_signed_pem(-3600, 3600 * 24 * 365);
  auto attacker = make_self_signed_pem(-3600, 3600 * 24 * 365);  // different key
  auto nc = make_named(trusted);
  crypto::cert_chain_t chain;
  chain.add(nc);

  auto x = crypto::x509(attacker);
  crypto::p_named_cert_t out;
  ASSERT_NE(chain.verify(x.get(), out), nullptr) << "an unpaired client cert must be rejected";
}
