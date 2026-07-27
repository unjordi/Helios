/**
 * @file src/crypto.h
 * @brief Declarations for cryptography functions.
 */
#pragma once

// standard includes
#include <array>

// lib includes
#include <list>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <nlohmann/json.hpp>

// local includes
#include "utility.h"

namespace crypto {
  /**
   * @brief PEM-encoded certificate and private key pair.
   */
  struct creds_t {
    std::string x509;  ///< PEM-encoded X.509 certificate.
    std::string pkey;  ///< Private key PEM string or path.
  };

  /**
   * @brief Destroy an OpenSSL message digest context.
   *
   * @param ctx Message digest context.
   */
  void md_ctx_destroy(EVP_MD_CTX *ctx);

  /**
   * @brief Fixed-size SHA-256 digest byte array.
   */
  using sha256_t = std::array<std::uint8_t, SHA256_DIGEST_LENGTH>;

  /**
   * @brief Byte buffer containing AES key material.
   */
  using aes_t = std::vector<std::uint8_t>;
  /**
   * @brief Owning pointer for an OpenSSL X.509 certificate.
   */
  using x509_t = util::safe_ptr<X509, X509_free>;
  /**
   * @brief Owning pointer for an OpenSSL certificate store.
   */
  using x509_store_t = util::safe_ptr<X509_STORE, X509_STORE_free>;
  /**
   * @brief Owning pointer for an OpenSSL certificate verification context.
   */
  using x509_store_ctx_t = util::safe_ptr<X509_STORE_CTX, X509_STORE_CTX_free>;
  /**
   * @brief Owning pointer for an OpenSSL cipher context.
   */
  using cipher_ctx_t = util::safe_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
  /**
   * @brief Owning pointer for an OpenSSL message digest context.
   */
  using md_ctx_t = util::safe_ptr<EVP_MD_CTX, md_ctx_destroy>;
  /**
   * @brief Owning pointer for an OpenSSL BIO chain.
   */
  using bio_t = util::safe_ptr<BIO, BIO_free_all>;
  /**
   * @brief Owning pointer for an OpenSSL public or private key.
   */
  using pkey_t = util::safe_ptr<EVP_PKEY, EVP_PKEY_free>;
  /**
   * @brief Owning pointer for an OpenSSL key-generation context.
   */
  using pkey_ctx_t = util::safe_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_free>;
  /**
   * @brief Owning pointer for an OpenSSL BIGNUM.
   */
  using bignum_t = util::safe_ptr<BIGNUM, BN_free>;

  /**
   * @brief The permissions of a client.
   */
  enum class PERM: uint32_t {
    _reserved        = 1,

    _input           = _reserved << 8,   // Input permission group
    input_controller = _input << 0,      // Allow controller input
    input_touch      = _input << 1,      // Allow touch input
    input_pen        = _input << 2,      // Allow pen input
    input_mouse      = _input << 3,      // Allow mouse input
    input_kbd        = _input << 4,      // Allow keyboard input
    _all_inputs      = input_controller | input_touch | input_pen | input_mouse | input_kbd,

    _operation       = _input << 8,      // Operation permission group
    clipboard_set    = _operation << 0,  // Allow set clipboard from client
    clipboard_read   = _operation << 1,  // Allow read clipboard from host
    file_upload      = _operation << 2,  // Allow upload files to host
    file_dwnload     = _operation << 3,  // Allow download files from host
    server_cmd       = _operation << 4,  // Allow execute server cmd
    _all_opeiations  = clipboard_set | clipboard_read | file_upload | file_dwnload | server_cmd,

    _action          = _operation << 8,  // Action permission group
    list             = _action << 0,     // Allow list apps
    view             = _action << 1,     // Allow view streams
    launch           = _action << 2,     // Allow launch apps
    _allow_view      = view | launch,    // If no view permission is granted, disconnect the device upon permission update
    _all_actions     = list | view | launch,

    _default         = view | list,      // Default permissions for new clients
    _no              = 0,                // No permissions are granted
    _all             = _all_inputs | _all_opeiations | _all_actions, // All current permissions
  };

  inline constexpr PERM
  operator&(PERM x, PERM y) {
    return static_cast<PERM>(static_cast<uint32_t>(x) & static_cast<uint32_t>(y));
  }

  inline constexpr bool
  operator!(PERM p) {
    return static_cast<uint32_t>(p) == 0;
  }

  struct command_entry_t {
    std::string cmd;
    bool elevated;

    // Serialize method using nlohmann::json
    static inline nlohmann::json serialize(const command_entry_t& entry) {
      nlohmann::json node;
      node["cmd"] = entry.cmd;
      node["elevated"] = entry.elevated;
      return node;
    }
  };

  /**
   * @brief Certificate entry associated with a client name, UUID and per-client settings.
   */
  struct named_cert_t {
    std::string name;  ///< Human-readable name for this client.
    std::string uuid;  ///< Persistent Moonlight client UUID associated with the certificate.
    std::string cert;  ///< Certificate PEM string.
    std::string display_mode;  ///< Per-client display-mode override, empty to use the client request.
    std::list<command_entry_t> do_cmds;  ///< Per-client commands run on connect.
    std::list<command_entry_t> undo_cmds;  ///< Per-client commands run on disconnect.
    PERM perm;  ///< Granular per-client permission mask.
    bool enable_legacy_ordering;  ///< Whether to pad app names so legacy clients keep the list order.
    bool allow_client_commands;  ///< Whether this client may run the configured commands.
    bool always_use_virtual_display;  ///< Whether this client always gets a virtual display.
    bool enabled = true;  ///< Whether this persisted client entry may connect at all (kill switch, orthogonal to `perm`).
  };

  using p_named_cert_t = std::shared_ptr<named_cert_t>;

  /**
   * @brief Hashes the given plaintext using SHA-256.
   * @param plaintext
   * @return The SHA-256 hash of the plaintext.
   */
  sha256_t hash(const std::string_view &plaintext);

  /**
   * @brief Derive the AES key used by the pairing protocol.
   *
   * @param salt Random salt used when deriving the pairing secret.
   * @param pin PIN supplied by the client during pairing.
   * @return Parsed or generated OpenSSL object or PEM data.
   */
  aes_t gen_aes_key(const std::array<uint8_t, 16> &salt, const std::string_view &pin);
  /**
   * @brief Parse PEM text into an X.509 certificate object.
   *
   * @param x Certificate object or PEM text, depending on the overload.
   * @return Parsed or generated OpenSSL object or PEM data.
   */
  x509_t x509(const std::string_view &x);
  /**
   * @brief Parse PEM text into an OpenSSL private key object.
   *
   * @param k PEM text containing a private key.
   * @return Parsed or generated OpenSSL object or PEM data.
   */
  pkey_t pkey(const std::string_view &k);
  /**
   * @brief Serialize an OpenSSL object to PEM text.
   *
   * @param x509 X.509 certificate object or PEM data.
   * @return Parsed or generated OpenSSL object or PEM data.
   */
  std::string pem(x509_t &x509);
  /**
   * @brief Serialize an OpenSSL object to PEM text.
   *
   * @param pkey Private key PEM data or private key file path.
   * @return Parsed or generated OpenSSL object or PEM data.
   */
  std::string pem(pkey_t &pkey);

  /**
   * @brief Sign data with SHA-256.
   *
   * @param pkey Private key PEM data or private key file path.
   * @param data Payload or state data to serialize, deserialize, or forward.
   * @return Number of bytes written, signature bytes, or an error status depending on the overload.
   */
  std::vector<uint8_t> sign256(const pkey_t &pkey, const std::string_view &data);
  /**
   * @brief Verify a SHA-256 signature with the certificate public key.
   *
   * @param x509 X.509 certificate object or PEM data.
   * @param data Payload or state data to serialize, deserialize, or forward.
   * @param signature Signature bytes to verify or encode.
   * @return True when the signature is valid for the supplied data.
   */
  bool verify256(const x509_t &x509, const std::string_view &data, const std::string_view &signature);

  /**
   * @brief Generate a self-signed certificate and private key for Sunshine pairing.
   *
   * @param cn Common name to place in the generated certificate.
   * @param key_bits Size in bits of the generated RSA key.
   * @return Parsed or generated OpenSSL object or PEM data.
   */
  creds_t gen_creds(const std::string_view &cn, std::uint32_t key_bits);

  /**
   * @brief Return the certificate signature bytes.
   *
   * @param x Certificate object or PEM text, depending on the overload.
   * @return Parsed or generated OpenSSL object or PEM data.
   */
  std::string_view signature(const x509_t &x);

  /**
   * @brief Generate cryptographically secure random bytes.
   *
   * @param bytes Number of random bytes to generate.
   * @return Random bytes or text generated by OpenSSL.
   */
  std::string rand(std::size_t bytes);
  /**
   * @brief Generate random text from the supplied alphabet.
   *
   * @param bytes Number of random bytes to generate.
   * @param alphabet Allowed characters used for random string generation.
   * @return Random bytes or text generated by OpenSSL.
   */
  std::string rand_alphabet(std::size_t bytes, const std::string_view &alphabet = std::string_view {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!%&()=-"});

  /**
   * @brief Owns the certificate chain returned by Sunshine's TLS certificate loader.
   */
  class cert_chain_t {
  public:
    KITTY_DECL_CONSTR(cert_chain_t)

    /**
     * @brief Add a named certificate to the verification chain.
     *
     * @param named_cert_p Named client certificate (cert + per-client metadata/permissions).
     */
    void add(p_named_cert_t& named_cert_p);

    /**
     * @brief Remove all certificates from the verification chain.
     */
    void clear();

    const char *verify(x509_t::element_type *cert, p_named_cert_t& named_cert_out);

  private:
    std::vector<std::pair<p_named_cert_t, x509_store_t>> _certs;
    x509_store_ctx_t _cert_ctx;
  };

  namespace cipher {
    constexpr std::size_t tag_size = 16;  ///< Tag size.

    /**
     * @brief Round a byte count up to the next PKCS#7 padding boundary.
     *
     * @param size Number of bytes or elements requested.
     * @return `size` rounded up to the next PKCS#7 block boundary.
     */
    constexpr std::size_t round_to_pkcs7_padded(std::size_t size) {
      return ((size + 15) / 16) * 16;
    }

    /**
     * @brief AES-GCM encrypt/decrypt context pair used for GameStream messages.
     */
    class cipher_t {
    public:
      cipher_ctx_t decrypt_ctx;  ///< Decrypt ctx.
      cipher_ctx_t encrypt_ctx;  ///< Encrypt ctx.

      aes_t key;  ///< AES key used by the cipher context.

      bool padding;  ///< Enables block padding for the cipher.
    };

    /**
     * @brief AES-ECB cipher helper used by pairing and stream encryption code.
     */
    class ecb_t: public cipher_t {
    public:
      ecb_t() = default;
      /**
       * @brief Construct an AES-ECB cipher helper with key material.
       */
      ecb_t(ecb_t &&) noexcept = default;
      /**
       * @brief Assign state from another instance while preserving ownership semantics.
       *
       * @return Reference or value produced by the operator.
       */
      ecb_t &operator=(ecb_t &&) noexcept = default;

      /**
       * @brief Construct an AES-ECB cipher helper with key material.
       *
       * @param key AES key material used to initialize the cipher.
       * @param padding Whether the cipher should use block padding.
       */
      ecb_t(const aes_t &key, bool padding = true);

      /**
       * @brief Encrypt plaintext into the supplied ciphertext buffer.
       *
       * @param plaintext Plaintext bytes to encrypt.
       * @param cipher Ciphertext bytes to decrypt or output buffer for encryption.
       * @return Number of bytes written, signature bytes, or an error status depending on the overload.
       */
      int encrypt(const std::string_view &plaintext, std::vector<std::uint8_t> &cipher);
      /**
       * @brief Decrypt ciphertext into the supplied plaintext buffer.
       *
       * @param cipher Ciphertext bytes to decrypt or output buffer for encryption.
       * @param plaintext Plaintext bytes to encrypt.
       * @return Number of bytes written, signature bytes, or an error status depending on the overload.
       */
      int decrypt(const std::string_view &cipher, std::vector<std::uint8_t> &plaintext);
    };

    /**
     * @brief AES-GCM cipher helper that encrypts and authenticates payloads.
     */
    class gcm_t: public cipher_t {
    public:
      gcm_t() = default;
      /**
       * @brief Construct an AES-GCM cipher helper with key material and IV state.
       */
      gcm_t(gcm_t &&) noexcept = default;
      /**
       * @brief Assign state from another instance while preserving ownership semantics.
       *
       * @return Reference or value produced by the operator.
       */
      gcm_t &operator=(gcm_t &&) noexcept = default;

      /**
       * @brief Construct an AES-GCM cipher helper with key material and IV state.
       *
       * @param key AES key material used to initialize the cipher.
       * @param padding Whether the cipher should use block padding.
       */
      gcm_t(const crypto::aes_t &key, bool padding = true);

      /**
       * @brief Encrypts the plaintext using AES GCM mode.
       * @param plaintext The plaintext data to be encrypted.
       * @param tag The buffer where the GCM tag will be written.
       * @param ciphertext The buffer where the resulting ciphertext will be written.
       * @param iv The initialization vector to be used for the encryption.
       * @return The total length of the ciphertext and GCM tag. Returns -1 in case of an error.
       */
      int encrypt(const std::string_view &plaintext, std::uint8_t *tag, std::uint8_t *ciphertext, aes_t *iv);

      /**
       * @brief Encrypts the plaintext using AES GCM mode.
       * length of cipher must be at least: round_to_pkcs7_padded(plaintext.size()) + crypto::cipher::tag_size
       * @param plaintext The plaintext data to be encrypted.
       * @param tagged_cipher The buffer where the resulting ciphertext and GCM tag will be written.
       * @param iv The initialization vector to be used for the encryption.
       * @return The total length of the ciphertext and GCM tag written into tagged_cipher. Returns -1 in case of an error.
       */
      int encrypt(const std::string_view &plaintext, std::uint8_t *tagged_cipher, aes_t *iv);

      /**
       * @brief Decrypt ciphertext into the supplied plaintext buffer.
       *
       * @param cipher Ciphertext bytes to decrypt or output buffer for encryption.
       * @param plaintext Plaintext bytes to encrypt.
       * @param iv Initialization vector for the cipher operation.
       * @return Number of bytes written, signature bytes, or an error status depending on the overload.
       */
      int decrypt(const std::string_view &cipher, std::vector<std::uint8_t> &plaintext, aes_t *iv);
    };

    /**
     * @brief AES-CBC cipher helper used for block-mode encryption and decryption.
     */
    class cbc_t: public cipher_t {
    public:
      cbc_t() = default;
      /**
       * @brief Construct an AES-CBC cipher helper with key material and IV state.
       */
      cbc_t(cbc_t &&) noexcept = default;
      /**
       * @brief Assign state from another instance while preserving ownership semantics.
       *
       * @return Reference or value produced by the operator.
       */
      cbc_t &operator=(cbc_t &&) noexcept = default;

      /**
       * @brief Construct an AES-CBC cipher helper with key material and IV state.
       *
       * @param key AES key material used to initialize the cipher.
       * @param padding Whether the cipher should use block padding.
       */
      cbc_t(const crypto::aes_t &key, bool padding = true);

      /**
       * @brief Encrypts the plaintext using AES CBC mode.
       * length of cipher must be at least: round_to_pkcs7_padded(plaintext.size())
       * @param plaintext The plaintext data to be encrypted.
       * @param cipher The buffer where the resulting ciphertext will be written.
       * @param iv The initialization vector to be used for the encryption.
       * @return The total length of the ciphertext written into cipher. Returns -1 in case of an error.
       */
      int encrypt(const std::string_view &plaintext, std::uint8_t *cipher, aes_t *iv);
    };
  }  // namespace cipher
}  // namespace crypto
