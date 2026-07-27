/**
 * @file src/confighttp.cpp
 * @brief Definitions for the Web UI Config HTTPS server.
 *
 * @todo Better handling of routes common to nvhttp, cleanup
 */
#define BOOST_BIND_GLOBAL_PLACEHOLDERS

// standard includes
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

// lib includes
#include <boost/algorithm/string.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <Simple-Web-Server/crypto.hpp>
#include <Simple-Web-Server/server_https.hpp>

#ifdef _WIN32
  #include "platform/windows/misc.h"

  #include <Windows.h>
#endif

// local includes
#include "config.h"
#include "confighttp.h"
#include "crypto.h"
#include "display_device.h"
#include "file_handler.h"
#include "globals.h"
#include "httpcommon.h"
#include "logging.h"
#include "network.h"
#include "nvhttp.h"
#include "platform/common.h"
#include "process.h"
#include "rtsp.h"
#include "utility.h"
#include "uuid.h"

#ifdef _WIN32
  #include "platform/windows/utils.h"
#endif

using namespace std::literals;

namespace confighttp {
  namespace fs = std::filesystem;

  /**
   * @brief Case-insensitive map used for HTTP headers and query parameters.
   */
  using args_t = SimpleWeb::CaseInsensitiveMultimap;

  /**
   * @brief Handler signature for configuration UI HTTPS routes.
   */
  using https_handler_t = std::function<void(resp_https_t, req_https_t)>;

  /**
   * @brief Client certificate operations accepted by the configuration API.
   */
  enum class op_e {
    ADD,  ///< Add client
    REMOVE  ///< Remove client
  };

  // Web UI session cookie (Helios login flow)
  std::string sessionCookie;  ///< Hash of the currently valid session cookie; empty when logged out.
  static std::chrono::time_point<std::chrono::steady_clock> cookie_creation_time;  ///< When the active session cookie was issued.

  // CSRF token management
  /**
   * @brief CSRF token value and its expiration deadline.
   */
  struct csrf_token_t {
    std::string token;  ///< Random token value that must be echoed by the client.
    std::chrono::steady_clock::time_point expiration;  ///< Monotonic deadline after which the token is rejected.
  };

  std::map<std::string, csrf_token_t, std::less<>> csrf_tokens;  ///< CSRF tokens by client identifier. NOSONAR(cpp:S5421) - intentionally mutable global
  std::mutex csrf_tokens_mutex;  ///< Mutex protecting CSRF token storage. NOSONAR(cpp:S5421) - intentionally mutable global

  // CSRF token configuration
  /**
   * @brief Number of random bytes used when generating a CSRF token.
   */
  constexpr auto CSRF_TOKEN_SIZE = 32;  // 32 bytes = 256 bits
  /**
   * @brief Amount of time a generated CSRF token remains valid.
   */
  constexpr auto CSRF_TOKEN_LIFETIME = std::chrono::hours(1);  // Tokens valid for 1 hour

  /**
   * @brief Log the request details.
   * @param request The HTTP request object.
   */
  void print_req(const req_https_t &request) {
    BOOST_LOG(debug) << "METHOD :: "sv << request->method;
    BOOST_LOG(debug) << "DESTINATION :: "sv << request->path;

    for (auto &[name, val] : request->header) {
      BOOST_LOG(debug) << name << " -- " << (name == "Authorization" ? "CREDENTIALS REDACTED" : val);
    }

    BOOST_LOG(debug) << " [--] "sv;

    for (auto &[name, val] : request->parse_query_string()) {
      BOOST_LOG(debug) << name << " -- " << val;
    }

    BOOST_LOG(debug) << " [--] "sv;
  }

  /**
   * @brief Send a response.
   * @param response The HTTP response object.
   * @param output_tree The JSON tree to send.
   */
  void send_response(const resp_https_t &response, const nlohmann::json &output_tree) {
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(output_tree.dump(), headers);
  }

  /**
   * @brief Send a 401 Unauthorized response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void send_unauthorized(const resp_https_t &response, const req_https_t &request) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- not authorized"sv;

    constexpr auto code = SimpleWeb::StatusCode::client_error_unauthorized;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["status"] = false;
    tree["error"] = "Unauthorized";

    const SimpleWeb::CaseInsensitiveMultimap headers {
      {"Content-Type", "application/json"},
      {"WWW-Authenticate", R"(Basic realm="Helios Gamestream Host", charset="UTF-8")"},
      {"X-Frame-Options", "DENY"},
      {"Content-Security-Policy", "frame-ancestors 'none';"}
    };

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Send a redirect response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param path The path to redirect to.
   */
  void send_redirect(const resp_https_t &response, const req_https_t &request, const char *path) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    BOOST_LOG(info) << "Web UI: ["sv << address << "] -- redirecting"sv;
    const SimpleWeb::CaseInsensitiveMultimap headers {
      {"Location", path},
      {"X-Frame-Options", "DENY"},
      {"Content-Security-Policy", "frame-ancestors 'none';"}
    };
    response->write(SimpleWeb::StatusCode::redirection_temporary_redirect, headers);
  }

  /**
   * @brief Retrieve the value of a key from a cookie string.
   * @param cookieString The cookie header string.
   * @param key The key to search.
   * @return The value if found, empty string otherwise.
   */
  std::string getCookieValue(const std::string &cookieString, const std::string &key) {
    const std::string keyWithEqual = key + "=";
    std::size_t startPos = cookieString.find(keyWithEqual);
    if (startPos == std::string::npos) {
      return "";
    }
    startPos += keyWithEqual.length();
    const std::size_t endPos = cookieString.find(';', startPos);
    if (endPos == std::string::npos) {
      return cookieString.substr(startPos);
    }
    return cookieString.substr(startPos, endPos - startPos);
  }

  /**
   * @brief Check whether the IP origin is allowed to reach the Web UI.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @return True if allowed, false otherwise.
   */
  bool checkIPOrigin(const resp_https_t &response, const req_https_t &request) {
    auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
    if (const auto ip_type = net::from_address(address); ip_type > http::origin_web_ui_allowed) {
      BOOST_LOG(info) << "Web UI: ["sv << address << "] -- denied"sv;
      response->write(SimpleWeb::StatusCode::client_error_forbidden);
      return false;
    }
    return true;
  }

  /**
   * @brief Whether the client is navigating with a browser rather than calling the REST API.
   *
   * Used to decide between redirecting an unauthenticated request to the login page (browser
   * navigation) and answering it with a plain 401 (API/script client).
   *
   * @param request The HTTP request object.
   * @return True when the request explicitly accepts HTML.
   */
  bool wants_html(const req_https_t &request) {
    const auto accept = request->header.find("accept");
    return accept != request->header.end() && accept->second.find("text/html") != std::string::npos;
  }

  /**
   * @brief Authenticate the request.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param needsRedirect Whether an unauthenticated browser navigation should be sent to the login page.
   * @return True if authenticated, false otherwise.
   *
   * Two credentials are accepted: the session cookie handed out by `/api/login` (the Helios
   * login flow) and HTTP Basic credentials (upstream behaviour, kept so that scripts and API
   * clients keep working).
   */
  bool authenticate(const resp_https_t &response, const req_https_t &request, bool needsRedirect) {
    if (!checkIPOrigin(response, request)) {
      return false;
    }

    // If credentials are not set yet, send the user to the welcome page
    if (config::sunshine.username.empty()) {
      send_redirect(response, request, "/welcome");
      return false;
    }

    auto fg = util::fail_guard([&]() {
      if (needsRedirect && wants_html(request)) {
        std::string redir_path = "/login?redir=.";
        redir_path += request->path;
        send_redirect(response, request, redir_path.c_str());
      } else {
        send_unauthorized(response, request);
      }
    });

    // 1. Session cookie issued by the Web UI login endpoint
    if (!sessionCookie.empty()) {
      if (std::chrono::steady_clock::now() - cookie_creation_time > SESSION_EXPIRE_DURATION) {
        sessionCookie.clear();
      } else if (const auto cookies = request->header.find("cookie"); cookies != request->header.end()) {
        if (const auto authCookie = getCookieValue(cookies->second, "auth");
            !authCookie.empty() &&
            util::hex(crypto::hash(authCookie + config::sunshine.salt)).to_string() == sessionCookie) {
          fg.disable();
          return true;
        }
      }
    }

    // 2. HTTP Basic credentials
    const auto auth = request->header.find("authorization");
    if (auth == request->header.end()) {
      return false;
    }

    const auto &rawAuth = auth->second;
    if (rawAuth.rfind("Basic "sv, 0) != 0) {
      return false;
    }

    const auto authData = SimpleWeb::Crypto::Base64::decode(rawAuth.substr("Basic "sv.length()));
    const auto colon = authData.find(':');
    if (colon == std::string::npos || colon + 1 >= authData.size()) {
      return false;
    }

    const auto username = authData.substr(0, colon);
    const auto password = authData.substr(colon + 1);

    if (const auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        !boost::iequals(username, config::sunshine.username) || hash != config::sunshine.password) {
      return false;
    }

    fg.disable();
    return true;
  }

  /**
   * @brief Send a 404 Not Found response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param error_message The error message to include in the response.
   */
  void not_found(const resp_https_t &response, [[maybe_unused]] const req_https_t &request, const std::string &error_message) {
    constexpr auto code = SimpleWeb::StatusCode::client_error_not_found;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["error"] = error_message;

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Send a 400 Bad Request response.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param error_message The error message to include in the response.
   */
  void bad_request(const resp_https_t &response, [[maybe_unused]] const req_https_t &request, const std::string &error_message) {
    constexpr auto code = SimpleWeb::StatusCode::client_error_bad_request;

    nlohmann::json tree;
    tree["status_code"] = code;
    tree["status"] = false;
    tree["error"] = error_message;

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "application/json");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

    response->write(code, tree.dump(), headers);
  }

  /**
   * @brief Validate the request content type and send a bad request when mismatched.
   */
  bool check_content_type(const resp_https_t &response, const req_https_t &request, const std::string_view &contentType) {
    const auto requestContentType = request->header.find("content-type");
    if (requestContentType == request->header.end()) {
      bad_request(response, request, "Content type not provided");
      return false;
    }

    // Extract the media type part before any parameters (e.g., charset)
    std::string actualContentType = requestContentType->second;
    if (const size_t semicolonPos = actualContentType.find(';'); semicolonPos != std::string::npos) {
      actualContentType = actualContentType.substr(0, semicolonPos);
    }

    // Trim whitespace and convert to lowercase for case-insensitive comparison
    boost::algorithm::trim(actualContentType);
    boost::algorithm::to_lower(actualContentType);

    std::string expectedContentType(contentType);
    boost::algorithm::to_lower(expectedContentType);

    if (actualContentType != expectedContentType) {
      bad_request(response, request, "Content type mismatch");
      return false;
    }
    return true;
  }

  /**
   * @brief Get a unique client identifier for CSRF token management.
   * @param request The HTTP request object.
   * @return An identifier derived from the session cookie, the username, or the IP address.
   */
  std::string get_client_id(const req_https_t &request) {
    // Prefer the Web UI session so tokens are scoped to a login rather than to a shared address
    if (const auto cookies = request->header.find("cookie"); cookies != request->header.end()) {
      if (const auto authCookie = getCookieValue(cookies->second, "auth"); !authCookie.empty()) {
        return util::hex(crypto::hash(authCookie + config::sunshine.salt)).to_string();
      }
    }

    // Then the authenticated username from HTTP Basic credentials
    if (const auto auth = request->header.find("authorization"); !config::sunshine.username.empty() && auth != request->header.end()) {
      if (const auto &rawAuth = auth->second; rawAuth.rfind("Basic "sv, 0) == 0) {
        const auto authData = SimpleWeb::Crypto::Base64::decode(rawAuth.substr("Basic "sv.length()));
        if (const auto colon = authData.find(':'); colon != std::string::npos && colon + 1 < authData.size()) {
          return authData.substr(0, colon);  // Return username
        }
      }
    }

    // Fall back to IP address if no username
    return net::addr_to_normalized_string(request->remote_endpoint().address());
  }

  /**
   * @brief Generate a new CSRF token for a client.
   * @param client_id A unique identifier for the client (e.g., session ID or username).
   * @return The generated CSRF token.
   */
  std::string generate_csrf_token(const std::string &client_id) {
    // Generate a cryptographically secure random token
    std::string token = crypto::rand_alphabet(CSRF_TOKEN_SIZE);

    std::scoped_lock lock(csrf_tokens_mutex);

    // Clean up expired tokens first
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(csrf_tokens, [&now](const auto &entry) {
      return entry.second.expiration < now;
    });

    // Store the token with expiration
    csrf_tokens[client_id] = csrf_token_t {
      token,
      now + CSRF_TOKEN_LIFETIME
    };

    return token;
  }

  /**
   * @brief Validate a stored CSRF token for a client against a provided token string.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param client_id A unique identifier for the client.
   * @param provided_token The token string to validate.
   * @return True if the token is valid, false otherwise.
   */
  bool validate_stored_csrf_token(const resp_https_t &response, const req_https_t &request, const std::string_view client_id, const std::string_view provided_token) {
    std::scoped_lock lock(csrf_tokens_mutex);
    const auto token_it = csrf_tokens.find(client_id);

    if (token_it == csrf_tokens.end()) {
      auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF token validation failed: no token found for client"sv;
      bad_request(response, request, "Invalid CSRF token");
      return false;
    }

    if (const auto now = std::chrono::steady_clock::now(); token_it->second.expiration < now) {
      csrf_tokens.erase(token_it);
      auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF token validation failed: token expired"sv;
      bad_request(response, request, "CSRF token expired");
      return false;
    }

    if (token_it->second.token != provided_token) {
      auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
      BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF token validation failed: token mismatch"sv;
      bad_request(response, request, "Invalid CSRF token");
      return false;
    }

    return true;
  }

  /**
   * @brief Validate CSRF token.
   */
  bool validate_csrf_token(const resp_https_t &response, const req_https_t &request, const std::string &client_id) {
    // Helper function to check if a URL starts with any allowed origin
    auto is_allowed_origin = [](const std::string_view url) {
      return std::ranges::any_of(config::sunshine.csrf_allowed_origins, [&url](const std::string &allowed_origin) {
        // Ensure exact prefix match (with ":" or "/" after to prevent malicious.com matching allowed.com)
        if (url.rfind(allowed_origin, 0) != 0) {  // rfind with pos=0 checks if the url starts with allowed_origin
          return false;
        }
        // Check that it's followed by ":" (port) or "/" (path) or is an exact match
        const size_t len = allowed_origin.length();
        return url.length() == len || url[len] == ':' || url[len] == '/';
      });
    };

    // Check if the request is from the same origin (Origin or Referer header matches configured allowed origins)
    const auto origin_it = request->header.find("Origin");
    if (origin_it != request->header.end() && is_allowed_origin(origin_it->second)) {
      // Same origin request - allow without CSRF token
      return true;
    }

    // If we have a Referer header, check if it's same-origin
    const auto referer_it = request->header.find("Referer");
    if (referer_it != request->header.end() && is_allowed_origin(referer_it->second)) {
      // Same origin request - allow without CSRF token
      return true;
    }

    // If neither Origin nor Referer is present, this cannot be a browser-initiated CSRF attack.
    // Non-browser clients (e.g. curl, scripts) never send these headers, and a malicious web page
    // cannot cause a non-browser client to make requests on a user's behalf.
    if (origin_it == request->header.end() && referer_it == request->header.end()) {
      return true;
    }

    // A browser-like request arrived with an Origin/Referer that doesn't match an allowed origin.
    // Require a CSRF token.
    const std::string_view blocked_origin = (origin_it != request->header.end()) ? origin_it->second : referer_it->second;
    // Extract token from X-CSRF-Token header
    const auto header_it = request->header.find("X-CSRF-Token");
    if (header_it == request->header.end()) {
      // Also check query parameters as fallback
      auto query_params = request->parse_query_string();
      const auto query_it = query_params.find("csrf_token");
      if (query_it == query_params.end()) {
        auto address = net::addr_to_normalized_string(request->remote_endpoint().address());
        BOOST_LOG(error) << "Web UI: ["sv << address << "] -- CSRF protection blocked request from origin: "sv << blocked_origin;
        BOOST_LOG(error) << "Web UI: To allow this origin, add it to the 'csrf_allowed_origins' option in your configuration"sv;
        bad_request(response, request, "Missing CSRF token");
        return false;
      }

      return validate_stored_csrf_token(response, request, client_id, query_it->second);
    }

    // Validate token from header
    return validate_stored_csrf_token(response, request, client_id, header_it->second);
  }

  /**
   * @brief Validates the application index and sends an error response if invalid.
   */
  bool check_app_index(const resp_https_t &response, const req_https_t &request, int index) {
    std::string file = file_handler::read_file(config::stream.file_apps.c_str());
    nlohmann::json file_tree = nlohmann::json::parse(file);
    if (const auto &apps = file_tree["apps"]; index < 0 || index >= static_cast<int>(apps.size())) {
      std::string error;
      if (const int max_index = static_cast<int>(apps.size()) - 1; max_index < 0) {
        error = "No applications found";
      } else {
        error = std::format("'index' {} out of range, max index is {}", index, max_index);
      }
      bad_request(response, request, error);
      return false;
    }
    return true;
  }

  /**
   * @brief Get an HTML page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @param html_file The HTML file to serve (relative to WEB_DIR).
   * @param require_auth Whether to require authentication (default: true).
   * @param redirect_if_username If true, redirect to "/" when the username is set (for welcome page).
   */
  void getPage(const resp_https_t &response, const req_https_t &request, const char *html_file, const bool require_auth, const bool redirect_if_username) {
    // Special handling for welcome page: redirect if the username is already set
    if (redirect_if_username && !config::sunshine.username.empty()) {
      send_redirect(response, request, "/");
      return;
    }

    // Browser navigations land on the login page; API clients get a 401
    if (require_auth && !authenticate(response, request, true)) {
      return;
    }

    print_req(request);

    const std::string content = file_handler::read_file((std::string(WEB_DIR) + html_file).c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");

    // prevent click jacking
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

    // The apps page pulls box art straight from IGDB
    if (std::string_view(html_file) == "apps.html"sv) {
      headers.emplace("Access-Control-Allow-Origin", "https://images.igdb.com/");
    }

    response->write(content, headers);
  }

  /**
   * @brief Get the login page.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * Unauthenticated by design: this is where `authenticate()` sends browsers.
   */
  void getLoginPage(const resp_https_t &response, const req_https_t &request) {
    if (!checkIPOrigin(response, request)) {
      return;
    }

    if (config::sunshine.username.empty()) {
      send_redirect(response, request, "/welcome");
      return;
    }

    print_req(request);

    const std::string content = file_handler::read_file(WEB_DIR "login.html");
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "text/html; charset=utf-8");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(content, headers);
  }

  /**
   * @brief Get the favicon image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @todo combine function with getLogoImage and possibly getAsset
   * @todo use mime_types map
   */
  void getFaviconImage(const resp_https_t &response, const req_https_t &request) {
    print_req(request);

    std::ifstream in(WEB_DIR "images/apollo.ico", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/x-icon");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  /**
   * @brief Get the logo image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @todo combine function with getFaviconImage and possibly getAsset
   * @todo use mime_types map
   */
  void getLogoImage(const resp_https_t &response, const req_https_t &request) {
    print_req(request);

    std::ifstream in(WEB_DIR "images/logo-apollo-45.png", std::ios::binary);
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", "image/png");
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  /**
   * @brief Check if a path is a child of another path.
   * @param base The base path.
   * @param query The path to check.
   * @return True if the path is a child of the base path, false otherwise.
   */
  bool isChildPath(fs::path const &base, fs::path const &query) {
    auto relPath = fs::relative(base, query);
    return *(relPath.begin()) != fs::path("..");
  }

  /**
   * @brief Get an asset.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   */
  void getAsset(const resp_https_t &response, const req_https_t &request) {
    print_req(request);
    fs::path webDirPath(WEB_DIR);
    fs::path nodeModulesPath(webDirPath / "assets");

    // .relative_path is needed to shed any leading slash that might exist in the request path
    auto filePath = fs::weakly_canonical(webDirPath / fs::path(request->path).relative_path());

    // Don't do anything if the file does not exist or is outside the assets directory
    if (!isChildPath(filePath, nodeModulesPath)) {
      BOOST_LOG(warning) << "Someone requested a path " << filePath << " that is outside the assets folder";
      bad_request(response, request);
      return;
    }
    if (!fs::exists(filePath)) {
      not_found(response, request);
      return;
    }

    auto relPath = fs::relative(filePath, webDirPath);
    // get the mime type from the file extension mime_types map
    // remove the leading period from the extension
    auto mimeType = mime_types.find(relPath.extension().string().substr(1));
    // check if the extension is in the map at the x position
    if (mimeType == mime_types.end()) {
      bad_request(response, request);
      return;
    }

    // if it is, set the content type to the mime type
    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("Content-Type", mimeType->second);
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    std::ifstream in(filePath.string(), std::ios::binary);
    response->write(SimpleWeb::StatusCode::success_ok, in, headers);
  }

  /**
   * @brief Get a CSRF token for the authenticated user.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/csrf-token| GET| null}
   */
  void getCSRFToken(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string client_id = get_client_id(request);
    std::string token = generate_csrf_token(client_id);

    nlohmann::json output_tree;
    output_tree["csrf_token"] = token;
    send_response(response, output_tree);
  }

  /**
   * @brief Get the list of available applications.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps| GET| null}
   */
  void getApps(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      std::string content = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(content);

      // Legacy versions used strings for booleans and integers, let's convert them
      // List of keys to convert to boolean
      const std::vector<std::string> boolean_keys = {
        "exclude-global-prep-cmd",
        "elevated",
        "auto-detach",
        "wait-all"
      };

      // List of keys to convert to integers
      const std::vector<std::string> integer_keys = {
        "exit-timeout"
      };

      // Walk fileTree and convert true/false strings to boolean or integer values
      for (auto &app : file_tree["apps"]) {
        for (const auto &key : boolean_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = app[key] == "true";
          }
        }
        for (const auto &key : integer_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = std::stoi(app[key].get<std::string>());
          }
        }
        if (app.contains("prep-cmd")) {
          for (auto &prep : app["prep-cmd"]) {
            if (prep.contains("elevated") && prep["elevated"].is_string()) {
              prep["elevated"] = prep["elevated"] == "true";
            }
          }
        }
      }

      file_tree["current_app"] = proc::proc.get_running_app_uuid();
      file_tree["host_uuid"] = http::unique_id;
      file_tree["host_name"] = config::nvhttp.sunshine_name;

      send_response(response, file_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "GetApps: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Save an application. To save a new application the UUID must be empty.
   *        To update an existing application, you must provide the current UUID of the application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "name": "Application Name",
   *   "output": "Log Output Path",
   *   "cmd": "Command to run the application",
   *   "exclude-global-prep-cmd": false,
   *   "elevated": false,
   *   "auto-detach": true,
   *   "wait-all": true,
   *   "exit-timeout": 5,
   *   "prep-cmd": [
   *     {
   *       "do": "Command to prepare",
   *       "undo": "Command to undo preparation",
   *       "elevated": false
   *     }
   *   ],
   *   "detached": [
   *     "Detached command"
   *   ],
   *   "image-path": "Full path to the application image. Must be a png file.",
   *   "uuid": "aaaa-bbbb"
   * }
   * @endcode
   *
   * @api_examples{/api/apps| POST| {"name":"Hello, World!","uuid": "aaaa-bbbb"}}
   */
  void saveApp(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();

    BOOST_LOG(info) << config::stream.file_apps;
    try {
      // TODO: Input Validation

      // Read the input JSON from the request body.
      nlohmann::json inputTree = nlohmann::json::parse(ss.str());

      // Read the existing apps file.
      std::string content = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json fileTree = nlohmann::json::parse(content);

      // Migrate/merge the new app into the file tree (UUID based).
      proc::migrate_apps(&fileTree, &inputTree);

      // Write the updated file tree back to disk.
      file_handler::write_file(config::stream.file_apps.c_str(), fileTree.dump(4));
      proc::refresh(config::stream.file_apps);

      // Prepare and send the output response.
      nlohmann::json outputTree;
      outputTree["status"] = true;
      send_response(response, outputTree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveApp: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Close the currently running application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/close| POST| null}
   */
  void closeApp(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    proc::proc.terminate();

    nlohmann::json output_tree;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Reorder applications.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/reorder| POST| {"order": ["aaaa-bbbb", "cccc-dddd"]}}
   */
  void reorderApps(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    try {
      std::stringstream ss;
      ss << request->content.rdbuf();

      nlohmann::json input_tree = nlohmann::json::parse(ss.str());
      nlohmann::json output_tree;

      // Read the existing apps file.
      std::string content = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json fileTree = nlohmann::json::parse(content);

      // Get the desired order of UUIDs from the request.
      if (!input_tree.contains("order") || !input_tree["order"].is_array()) {
        throw std::runtime_error("Missing or invalid 'order' array in request body");
      }
      const auto &order_uuids_json = input_tree["order"];

      // Get the original apps array from the fileTree.
      // Default to an empty array if "apps" key is missing or if it's present but not an array (after logging an error).
      nlohmann::json original_apps_list = nlohmann::json::array();
      if (fileTree.contains("apps")) {
        if (fileTree["apps"].is_array()) {
          original_apps_list = fileTree["apps"];
        } else {
          // "apps" key exists but is not an array. This is a malformed state.
          BOOST_LOG(error) << "ReorderApps: 'apps' key in apps configuration file ('" << config::stream.file_apps
                           << "') is present but not an array.";
          throw std::runtime_error("'apps' in file is not an array, cannot reorder.");
        }
      } else {
        // "apps" key is missing. Treat as an empty list. Reordering an empty list is valid.
        BOOST_LOG(debug) << "ReorderApps: 'apps' key missing in apps configuration file ('" << config::stream.file_apps
                         << "'). Treating as an empty list for reordering.";
        // original_apps_list is already an empty array, so no specific action needed here.
      }

      nlohmann::json reordered_apps_list = nlohmann::json::array();
      std::vector<bool> item_moved(original_apps_list.size(), false);

      // Phase 1: Place apps according to the 'order' array from the request.
      // Iterate through the desired order of UUIDs.
      for (const auto &uuid_json_value : order_uuids_json) {
        if (!uuid_json_value.is_string()) {
          BOOST_LOG(warning) << "ReorderApps: Encountered a non-string UUID in the 'order' array. Skipping this entry.";
          continue;
        }
        std::string target_uuid = uuid_json_value.get<std::string>();
        bool found_match_for_ordered_uuid = false;

        // Find the first unmoved app in the original list that matches the current target_uuid.
        for (size_t i = 0; i < original_apps_list.size(); ++i) {
          if (item_moved[i]) {
            continue;  // This specific app object has already been placed.
          }

          const auto &app_item = original_apps_list[i];
          // Ensure the app item is an object and has a UUID to match against.
          if (app_item.is_object() && app_item.contains("uuid") && app_item["uuid"].is_string()) {
            if (app_item["uuid"].get<std::string>() == target_uuid) {
              reordered_apps_list.push_back(app_item);  // Add the found app object to the new list.
              item_moved[i] = true;  // Mark this specific object as moved.
              found_match_for_ordered_uuid = true;
              break;  // Found an app for this UUID, move to the next UUID in the 'order' array.
            }
          }
        }

        if (!found_match_for_ordered_uuid) {
          // This means a UUID specified in the 'order' array was not found in the original_apps_list
          // among the currently available (unmoved) app objects.
          // Per instruction "If the uuid is missing from the original json file, omit it."
          BOOST_LOG(debug) << "ReorderApps: UUID '" << target_uuid << "' from 'order' array not found in available apps list or its matching app was already processed. Omitting.";
        }
      }

      // Phase 2: Append any remaining apps from the original list that were not explicitly ordered.
      // These are app objects that were not marked 'item_moved' in Phase 1.
      for (size_t i = 0; i < original_apps_list.size(); ++i) {
        if (!item_moved[i]) {
          reordered_apps_list.push_back(original_apps_list[i]);
        }
      }

      // Update the fileTree with the new, reordered list of apps.
      fileTree["apps"] = reordered_apps_list;

      // Write the modified fileTree back to the apps configuration file.
      file_handler::write_file(config::stream.file_apps.c_str(), fileTree.dump(4));

      // Notify relevant parts of the system that the apps configuration has changed.
      proc::refresh(config::stream.file_apps);

      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "ReorderApps: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Delete an application.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/delete | POST| { uuid: 'aaaa-bbbb' }}
   */
  void deleteApp(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    try {
      std::stringstream ss;
      ss << request->content.rdbuf();
      nlohmann::json input_tree = nlohmann::json::parse(ss.str());

      // Check for required uuid field in body
      if (!input_tree.contains("uuid") || !input_tree["uuid"].is_string()) {
        bad_request(response, request, "Missing or invalid uuid in request body");
        return;
      }
      auto uuid = input_tree["uuid"].get<std::string>();

      // Read the apps file into a nlohmann::json object.
      std::string content = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json fileTree = nlohmann::json::parse(content);

      // Remove any app with the matching uuid directly from the "apps" array.
      if (fileTree.contains("apps") && fileTree["apps"].is_array()) {
        auto &apps = fileTree["apps"];
        apps.erase(
          std::remove_if(apps.begin(), apps.end(), [&uuid](const nlohmann::json &app) {
            return app.value("uuid", "") == uuid;
          }),
          apps.end()
        );
      }

      // Write the updated JSON back to the file.
      file_handler::write_file(config::stream.file_apps.c_str(), fileTree.dump(4));
      proc::refresh(config::stream.file_apps);

      // Prepare and send the response.
      nlohmann::json outputTree;
      outputTree["status"] = true;
      send_response(response, outputTree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "DeleteApp: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the list of paired clients.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clients/list| GET| null}
   */
  void getClients(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    const nlohmann::json named_certs = nvhttp::get_all_clients();

    nlohmann::json output_tree;
    output_tree["named_certs"] = named_certs;
#ifdef _WIN32
    output_tree["platform"] = "windows";
#endif
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Update client information: per-client permissions, commands and display mode.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * The body for the POST request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "uuid": "<uuid>",
   *   "name": "<Friendly Name>",
   *   "display_mode": "1920x1080x59.94",
   *   "do": [ { "cmd": "<command>", "elevated": false }, ... ],
   *   "undo": [ { "cmd": "<command>", "elevated": false }, ... ],
   *   "perm": <uint32_t>,
   *   "enabled": true
   * }
   * @endcode
   *
   * @note `enabled` is optional; when present the client is enabled/disabled and the live
   *       sessions of a disabled client are torn down.
   *
   * @api_examples{/api/clients/update| POST| {"uuid":"<uuid>","perm":0}}
   */
  void updateClient(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json input_tree = nlohmann::json::parse(ss.str());
      nlohmann::json output_tree;
      std::string uuid = input_tree.value("uuid", "");
      std::string name = input_tree.value("name", "");
      std::string display_mode = input_tree.value("display_mode", "");
      bool enable_legacy_ordering = input_tree.value("enable_legacy_ordering", true);
      bool allow_client_commands = input_tree.value("allow_client_commands", true);
      bool always_use_virtual_display = input_tree.value("always_use_virtual_display", false);
      auto do_cmds = nvhttp::extract_command_entries(input_tree, "do");
      auto undo_cmds = nvhttp::extract_command_entries(input_tree, "undo");
      auto perm = static_cast<crypto::PERM>(input_tree.value("perm", static_cast<uint32_t>(crypto::PERM::_no)) & static_cast<uint32_t>(crypto::PERM::_all));
      output_tree["status"] = nvhttp::update_device_info(
        uuid,
        name,
        display_mode,
        do_cmds,
        undo_cmds,
        perm,
        enable_legacy_ordering,
        allow_client_commands,
        always_use_virtual_display
      );

      // Optional enable/disable toggle: disabling a client also tears down its live sessions.
      if (input_tree.contains("enabled")) {
        const bool enabled = input_tree.value("enabled", true);
        if (nvhttp::set_client_enabled(uuid, enabled) && !enabled) {
          if (const auto cert = nvhttp::get_cert_by_uuid(uuid); !cert.empty()) {
            rtsp_stream::terminate_sessions_by_cert(cert);
          }

          if (rtsp_stream::session_count() == 0 && proc::proc.running() > 0) {
            proc::proc.terminate();
          }
        }
      }

      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "Update Client: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Unpair a client.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * The body for the POST request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *  "uuid": "<uuid>"
   * }
   * @endcode
   *
   * @api_examples{/api/clients/unpair| POST| {"uuid":"1234"}}
   */
  void unpair(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json output_tree;
      const nlohmann::json input_tree = nlohmann::json::parse(ss.str());
      const std::string uuid = input_tree.value("uuid", "");
      const bool removed = nvhttp::unpair_client(uuid);
      output_tree["status"] = removed;

      if (removed && nvhttp::get_all_clients().empty()) {
        proc::proc.terminate();
      }

      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "Unpair: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Unpair all clients.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clients/unpair-all| POST| null}
   */
  void unpairAll(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    nvhttp::erase_all_clients();
    proc::proc.terminate();

    nlohmann::json output_tree;
    output_tree["status"] = true;
    send_response(response, output_tree);
  }

  /**
   * @brief Disconnect a client.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/clients/disconnect| POST| {"uuid":"1234"}}
   */
  void disconnect(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    try {
      std::stringstream ss;
      ss << request->content.rdbuf();
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss.str());
      std::string uuid = input_tree.value("uuid", "");
      output_tree["status"] = nvhttp::find_and_stop_session(uuid, true);
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "Disconnect: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the configuration settings.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/config| GET| null}
   */
  void getConfig(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["platform"] = SUNSHINE_PLATFORM;
    output_tree["version"] = PROJECT_VERSION;
#ifdef _WIN32
    output_tree["vdisplayStatus"] = static_cast<int>(proc::vDisplayDriverStatus);
#endif

    auto vars = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));

    for (auto &[name, value] : vars) {
      output_tree[name] = std::move(value);
    }

    send_response(response, output_tree);
  }

  /**
   * @brief Get the locale setting. This endpoint does not require authentication.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/configLocale| GET| null}
   */
  void getLocale(const resp_https_t &response, const req_https_t &request) {
    // we need to return the locale whether authenticated or not

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = true;
    output_tree["locale"] = config::sunshine.locale;
    send_response(response, output_tree);
  }

  /**
   * @brief Save the configuration settings.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the POST request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "key": "value"
   * }
   * @endcode
   *
   * @attention{It is recommended to ONLY save the config settings that differ from the default behavior.}
   *
   * @api_examples{/api/config| POST| {"key":"value"}}
   */
  void saveConfig(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      std::stringstream config_stream;
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      for (const auto &[k, v] : input_tree.items()) {
        if (v.is_null() || (v.is_string() && v.get<std::string>().empty())) {
          continue;
        }

        // v.dump() will dump valid json, which we do not want for strings in the config, right now
        // we should migrate the config file to straight JSON and get rid of all this nonsense
        config_stream << k << " = " << (v.is_string() ? v.get<std::string>() : v.dump()) << std::endl;
      }
      file_handler::write_file(config::sunshine.config_file.c_str(), config_stream.str());
      output_tree["status"] = true;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SaveConfig: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get an application's image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @note{The index in the url path is the application index.}
   *
   * @api_examples{/api/covers/9999 | GET| null}
   */
  void getCover(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      const int index = std::stoi(request->path_match[1]);
      if (!check_app_index(response, request, index)) {
        return;
      }

      std::string file = file_handler::read_file(config::stream.file_apps.c_str());
      nlohmann::json file_tree = nlohmann::json::parse(file);
      auto &apps = file_tree["apps"];

      auto &app = apps[index];

      // Get the image path from the app configuration
      std::string app_image_path;
      if (app.contains("image-path") && !app["image-path"].is_null()) {
        app_image_path = app["image-path"];
      }

      // Use validate_app_image_path to resolve and validate the path
      // This handles extension validation, PNG signature validation, and path resolution
      std::string validated_path = proc::validate_app_image_path(app_image_path);

      // Check if we got the default image path (means validation failed or no image configured)
      if (validated_path == DEFAULT_APP_IMAGE_PATH) {
        BOOST_LOG(debug) << "Application at index " << index << " does not have a valid cover image";
        not_found(response, request, "Cover image not found");
        return;
      }

      // Open and stream the validated file
      std::ifstream in(validated_path, std::ios::binary);
      if (!in) {
        BOOST_LOG(warning) << "Unable to read cover image file: " << validated_path;
        bad_request(response, request, "Unable to read cover image file");
        return;
      }

      SimpleWeb::CaseInsensitiveMultimap headers;
      headers.emplace("Content-Type", "image/png");
      headers.emplace("X-Frame-Options", "DENY");
      headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");

      response->write(SimpleWeb::StatusCode::success_ok, in, headers);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "GetCover: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Upload a cover image.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "key": "igdb_<game_id>",
   *   "url": "https://images.igdb.com/igdb/image/upload/t_cover_big_2x/<slug>.png"
   * }
   * @endcode
   *
   * @api_examples{/api/covers/upload| POST| {"key":"igdb_1234","url":"https://images.igdb.com/igdb/image/upload/t_cover_big_2x/abc123.png"}}
   */
  void uploadCover(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);

      std::string key = input_tree.value("key", "");
      if (key.empty()) {
        bad_request(response, request, "Cover key is required");
        return;
      }
      std::string url = input_tree.value("url", "");

      const std::string coverdir = platf::appdata().string() + "/covers/";
      file_handler::make_directory(coverdir);

      const std::string path = coverdir + http::url_escape(key) + ".png";
      if (!url.empty()) {
        if (http::url_get_host(url) != "images.igdb.com") {
          bad_request(response, request, "Only images.igdb.com is allowed");
          return;
        }
        if (!http::download_file(url, path)) {
          bad_request(response, request, "Failed to download cover");
          return;
        }
      } else {
        auto data = SimpleWeb::Crypto::Base64::decode(input_tree.value("data", ""));

        std::ofstream imgfile(path);
        imgfile.write(data.data(), static_cast<int>(data.size()));
      }
      output_tree["status"] = true;
      output_tree["path"] = path;
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "UploadCover: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get the logs from the log file.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/logs| GET| null}
   */
  void getLogs(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    std::string content = file_handler::read_file(config::sunshine.log_file.c_str());
    SimpleWeb::CaseInsensitiveMultimap headers;
    std::string contentType = "text/plain";
#ifdef _WIN32
    contentType += "; charset=";
    contentType += currentCodePageToCharset();
#endif
    headers.emplace("Content-Type", contentType);
    headers.emplace("X-Frame-Options", "DENY");
    headers.emplace("Content-Security-Policy", "frame-ancestors 'none';");
    response->write(SimpleWeb::StatusCode::success_ok, content, headers);
  }

  /**
   * @brief Update existing credentials.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "currentUsername": "Current Username",
   *   "currentPassword": "Current Password",
   *   "newUsername": "New Username",
   *   "newPassword": "New Password",
   *   "confirmNewPassword": "Confirm New Password"
   * }
   * @endcode
   *
   * @api_examples{/api/password| POST| {"currentUsername":"admin","currentPassword":"admin","newUsername":"admin","newPassword":"admin","confirmNewPassword":"admin"}}
   */
  void savePassword(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!config::sunshine.username.empty() && !authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::vector<std::string> errors = {};
    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      // TODO: Input Validation
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      std::string username = input_tree.value("currentUsername", "");
      std::string newUsername = input_tree.value("newUsername", "");
      std::string password = input_tree.value("currentPassword", "");
      std::string newPassword = input_tree.value("newPassword", "");
      std::string confirmPassword = input_tree.value("confirmNewPassword", "");
      if (newUsername.empty()) {
        newUsername = username;
      }
      if (newUsername.empty()) {
        errors.emplace_back("Invalid Username");
      } else {
        auto hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
        if (config::sunshine.username.empty() || (boost::iequals(username, config::sunshine.username) && hash == config::sunshine.password)) {
          if (newPassword.empty() || newPassword != confirmPassword) {
            errors.emplace_back("Password Mismatch");
          } else {
            http::save_user_creds(config::sunshine.credentials_file, newUsername, newPassword);
            http::reload_user_creds(config::sunshine.credentials_file);
            sessionCookie.clear();  // force re-login
            output_tree["status"] = true;
          }
        } else {
          errors.emplace_back("Invalid Current Credentials");
        }
      }

      if (!errors.empty()) {
        // join the errors array
        std::string error = std::accumulate(errors.begin(), errors.end(), std::string(), [](const std::string &a, const std::string &b) {
          return a.empty() ? b : a + ", " + b;
        });
        bad_request(response, request, error);
        return;
      }

      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePassword: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Get a one-time password (OTP) that lets a client pair without a PIN prompt.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/otp| POST| {"passphrase":"secret","deviceName":"My PC"}}
   */
  void getOTP(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    try {
      std::stringstream ss;
      ss << request->content.rdbuf();
      nlohmann::json input_tree = nlohmann::json::parse(ss.str());

      std::string passphrase = input_tree.value("passphrase", "");
      if (passphrase.empty()) {
        throw std::runtime_error("Passphrase not provided!");
      }
      if (passphrase.size() < 4) {
        throw std::runtime_error("Passphrase too short!");
      }

      std::string deviceName = input_tree.value("deviceName", "");
      output_tree["otp"] = nvhttp::request_otp(passphrase, deviceName);
      output_tree["ip"] = platf::get_local_ip_for_gateway();
      output_tree["name"] = config::nvhttp.sunshine_name;
      output_tree["status"] = true;
      output_tree["message"] = "OTP created, effective within 3 minutes.";
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "OTP creation failed: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Send a pin code to the host. The pin is generated from the Moonlight client during the pairing process.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * The body for the post request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "pin": "<pin>",
   *   "name": "Friendly Client Name"
   * }
   * @endcode
   *
   * @api_examples{/api/pin| POST| {"pin":"1234","name":"My PC"}}
   */
  void savePin(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    std::stringstream ss;
    ss << request->content.rdbuf();
    try {
      nlohmann::json output_tree;
      nlohmann::json input_tree = nlohmann::json::parse(ss);
      const std::string name = input_tree.value("name", "");
      const std::string pin = input_tree.value("pin", "");

      if (const int parsed_pin = std::stoi(pin); parsed_pin < 0 || parsed_pin > 9999) {
        bad_request(response, request, "PIN must be between 0000 and 9999");
        return;
      }

      output_tree["status"] = nvhttp::pin(pin, name);
      send_response(response, output_tree);
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "SavePin: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Reset the display device persistence.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/reset-display-device-persistence| POST| null}
   */
  void resetDisplayDevicePersistence(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;
    output_tree["status"] = display_device::reset_persistence();
    send_response(response, output_tree);
  }

  /**
   * @brief Authenticate a Web UI request and restart the host process.
   *
   * @param response HTTP response used for authentication or CSRF failures.
   * @param request HTTP request carrying the client identity and CSRF token.
   *
   * @api_examples{/api/restart| POST| null}
   */
  void restart(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    proc::proc.terminate();

    // We may not return from this call
    platf::restart();
  }

  /**
   * @brief Quit the host process.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * On Windows, if running in a service, a special shutdown code is returned.
   *
   * @api_examples{/api/quit| POST| null}
   */
  void quit(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    BOOST_LOG(warning) << "Requested quit from config page!"sv;

    proc::proc.terminate();

#ifdef _WIN32
    if (GetConsoleWindow() == NULL) {
      lifetime::exit_sunshine(ERROR_SHUTDOWN_IN_PROGRESS, true);
    } else
#endif
    {
      lifetime::exit_sunshine(0, true);
    }

    // If exit fails, write a response after 5 seconds.
    std::thread write_resp([response] {
      std::this_thread::sleep_for(5s);
      response->write();
    });
    write_resp.detach();
  }

  /**
   * @brief Launch an application from the Web UI, addressed by its UUID.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/apps/launch| POST| {"uuid":"aaaa-bbbb"}}
   */
  void launchApp(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    try {
      std::stringstream ss;
      ss << request->content.rdbuf();
      nlohmann::json input_tree = nlohmann::json::parse(ss.str());

      // Check for required uuid field in body
      if (!input_tree.contains("uuid") || !input_tree["uuid"].is_string()) {
        bad_request(response, request, "Missing or invalid uuid in request body");
        return;
      }
      std::string uuid = input_tree["uuid"].get<std::string>();

      nlohmann::json output_tree;
      const auto &apps = proc::proc.get_apps();
      for (auto &app : apps) {
        if (app.uuid == uuid) {
          crypto::named_cert_t named_cert {
            .name = "",
            .uuid = http::unique_id,
            .perm = crypto::PERM::_all,
          };
          BOOST_LOG(info) << "Launching app ["sv << app.name << "] from web UI"sv;
          auto launch_session = nvhttp::make_launch_session(true, false, request->parse_query_string(), &named_cert);
          if (const auto err = proc::proc.execute(app, launch_session)) {
            bad_request(response, request, err == 503 ? "Failed to initialize video capture/encoding. Is a display connected and turned on?" : "Failed to start the specified application");
          } else {
            output_tree["status"] = true;
            send_response(response, output_tree);
          }
          return;
        }
      }
      BOOST_LOG(error) << "Couldn't find app with uuid ["sv << uuid << ']';
      bad_request(response, request, "Cannot find requested application");
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "LaunchApp: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Log the user in and hand out a session cookie.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * The body for the POST request should be JSON serialized in the following format:
   * @code{.json}
   * {
   *   "username": "<username>",
   *   "password": "<password>"
   * }
   * @endcode
   *
   * @api_examples{/api/login| POST| {"username":"admin","password":"admin"}}
   */
  void login(const resp_https_t &response, const req_https_t &request) {
    if (!checkIPOrigin(response, request) || !check_content_type(response, request, "application/json")) {
      return;
    }

    auto fg = util::fail_guard([&] {
      response->write(SimpleWeb::StatusCode::client_error_unauthorized);
    });

    try {
      std::stringstream ss;
      ss << request->content.rdbuf();
      nlohmann::json input_tree = nlohmann::json::parse(ss.str());
      std::string username = input_tree.value("username", "");
      std::string password = input_tree.value("password", "");
      std::string hash = util::hex(crypto::hash(password + config::sunshine.salt)).to_string();
      if (!boost::iequals(username, config::sunshine.username) || hash != config::sunshine.password) {
        return;
      }

      std::string sessionCookieRaw = crypto::rand_alphabet(64);
      sessionCookie = util::hex(crypto::hash(sessionCookieRaw + config::sunshine.salt)).to_string();
      cookie_creation_time = std::chrono::steady_clock::now();

      const SimpleWeb::CaseInsensitiveMultimap headers {
        {"Set-Cookie", "auth=" + sessionCookieRaw + "; Secure; SameSite=Strict; Max-Age=2592000; Path=/"}
      };
      response->write(headers);
      fg.disable();
    } catch (std::exception &e) {
      BOOST_LOG(warning) << "Web UI Login failed: ["sv << net::addr_to_normalized_string(request->remote_endpoint().address())
                         << "]: "sv << e.what();
      response->write(SimpleWeb::StatusCode::server_error_internal_server_error);
      fg.disable();
    }
  }

  /**
   * @brief Get ViGEmBus driver version and installation status.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/vigembus/status| GET| null}
   */
  void getViGEmBusStatus(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;

#ifdef _WIN32
    std::string version_str;
    bool installed = false;
    bool version_compatible = false;

    // Check if ViGEmBus driver exists
    std::filesystem::path driver_path = std::filesystem::path(std::getenv("SystemRoot") ? std::getenv("SystemRoot") : "C:\\Windows") / "System32" / "drivers" / "ViGEmBus.sys";

    if (std::filesystem::exists(driver_path)) {
      installed = platf::getFileVersionInfo(driver_path, version_str);
      if (installed) {
        // Parse version string to check compatibility (>= 1.17.0.0)
        std::vector<std::string> version_parts;
        std::stringstream ss(version_str);
        std::string part;
        while (std::getline(ss, part, '.')) {
          version_parts.push_back(part);
        }

        if (version_parts.size() >= 2) {
          int major = std::stoi(version_parts[0]);
          int minor = std::stoi(version_parts[1]);
          version_compatible = (major > 1) || (major == 1 && minor >= 17);
        }
      }
    }

    output_tree["installed"] = installed;
    output_tree["version"] = version_str;
    output_tree["version_compatible"] = version_compatible;
    output_tree["packaged_version"] = VIGEMBUS_PACKAGED_VERSION;
#else
    output_tree["error"] = "ViGEmBus is only available on Windows";
    output_tree["installed"] = false;
    output_tree["version"] = "";
    output_tree["version_compatible"] = false;
    output_tree["packaged_version"] = "";
#endif

    send_response(response, output_tree);
  }

  /**
   * @brief Install ViGEmBus driver with elevated permissions.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   *
   * @api_examples{/api/vigembus/install| POST| null}
   */
  void installViGEmBus(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    const std::string client_id = get_client_id(request);
    if (!validate_csrf_token(response, request, client_id)) {
      return;
    }

    print_req(request);

    nlohmann::json output_tree;

#ifdef _WIN32
    // Get the path to the packaged ViGEmBus installer.
    const std::filesystem::path installer_path = platf::appdata().parent_path() / "third-party" / "vigembus_installer.exe";

    if (!std::filesystem::exists(installer_path)) {
      output_tree["status"] = false;
      output_tree["error"] = "ViGEmBus installer not found";
      send_response(response, output_tree);
      return;
    }

    // Run the installer with elevated permissions
    std::error_code ec;
    boost::filesystem::path working_dir = boost::filesystem::path(installer_path.string()).parent_path();
    boost::process::v1::environment env = boost::this_process::environment();

    // Run with elevated permissions, non-interactive
    const std::string install_cmd = std::format("{} /quiet", installer_path.string());
    auto child = platf::run_command(true, false, install_cmd, working_dir, env, nullptr, ec, nullptr);

    if (ec) {
      output_tree["status"] = false;
      output_tree["error"] = "Failed to start installer: " + ec.message();
      send_response(response, output_tree);
      return;
    }

    // Wait for the installer to complete
    child.wait(ec);

    if (ec) {
      output_tree["status"] = false;
      output_tree["error"] = "Installer failed: " + ec.message();
    } else {
      int exit_code = child.exit_code();
      output_tree["status"] = (exit_code == 0);
      output_tree["exit_code"] = exit_code;
      if (exit_code != 0) {
        output_tree["error"] = std::format("Installer exited with code {}", exit_code);
      }
    }
#else
    output_tree["status"] = false;
    output_tree["error"] = "ViGEmBus installation is only available on Windows";
#endif

    send_response(response, output_tree);
  }

  /**
   * @brief Checks whether a directory entry qualifies as an executable file.
   * @param entry The directory entry to check.
   * @param status The cached file status for the entry.
   * @return True if the file should be included in an executable-type listing.
   */
  bool is_browsable_executable([[maybe_unused]] const fs::directory_entry &entry, [[maybe_unused]] const fs::file_status &status) {
#ifdef _WIN32
    auto ext = entry.path().extension().string();
    boost::algorithm::to_lower(ext);
    return ext == ".exe" || ext == ".bat" || ext == ".cmd" || ext == ".com" || ext == ".ps1";
#else
    const auto perms = status.permissions();
    return (perms & fs::perms::owner_exec) != fs::perms::none ||
           (perms & fs::perms::group_exec) != fs::perms::none ||
           (perms & fs::perms::others_exec) != fs::perms::none;
#endif
  }

#ifdef _WIN32
  /**
   * @brief Builds a JSON array of available Windows drive letters.
   * @return JSON array of drive-letter entries.
   */
  nlohmann::json get_windows_drives() {
    nlohmann::json entries = nlohmann::json::array();
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
      if (drives & (1 << i)) {
        const auto drive_letter = static_cast<char>('A' + i);
        const auto drive_path = std::string(1, drive_letter) + ":\\";
        nlohmann::json entry;
        entry["name"] = drive_path;
        entry["type"] = "directory";
        entry["path"] = drive_path;
        entries.push_back(entry);
      }
    }
    return entries;
  }
#endif

  /**
   * @brief Lists, filters, and sorts the entries of a directory for the browse API.
   * @param dir_path The directory to list.
   * @param type_str Filter type: "directory", "executable", "file", or "any".
   * @return Sorted JSON array of entry objects with name/type/path fields.
   */
  nlohmann::json build_browse_entries(const fs::path &dir_path, const std::string &type_str) {
    nlohmann::json entries = nlohmann::json::array();

    std::error_code iter_ec;
    for (auto it = fs::directory_iterator(dir_path, fs::directory_options::skip_permission_denied, iter_ec);
         !iter_ec && it != fs::directory_iterator();
         it.increment(iter_ec)) {
      try {
        const auto status = it->status();
        const bool is_dir = fs::is_directory(status);

        if (const bool is_regular = fs::is_regular_file(status); !is_dir && !is_regular) {
          continue;
        }

        // Apply type filter (directories are always included for navigation)
        if (type_str == "directory" && !is_dir) {
          continue;
        }

        if (type_str == "executable" && !is_dir && !is_browsable_executable(*it, status)) {
          continue;
        }

        nlohmann::json file_entry;
        file_entry["name"] = it->path().filename().string();
        file_entry["path"] = it->path().string();
        file_entry["type"] = is_dir ? "directory" : "file";
        entries.push_back(file_entry);
      } catch (const fs::filesystem_error &e) {
        BOOST_LOG(debug) << "BrowseDirectory: skipping entry due to error: "sv << e.what();
      }
    }

    if (iter_ec) {
      BOOST_LOG(debug) << "BrowseDirectory: directory iteration error: "sv << iter_ec.message();
    }

    // Sort: directories first, then files; both case-insensitively alphabetical
    std::sort(entries.begin(), entries.end(), [](const nlohmann::json &a, const nlohmann::json &b) {
      const bool a_dir = (a["type"] == "directory");
      if (const bool b_dir = (b["type"] == "directory"); a_dir != b_dir) {
        return a_dir && !b_dir;
      }
      auto a_name = a["name"].get<std::string>();
      auto b_name = b["name"].get<std::string>();
      boost::algorithm::to_lower(a_name);
      boost::algorithm::to_lower(b_name);
      return a_name < b_name;
    });

    return entries;
  }

  /**
   * @brief Browse the server filesystem.
   * @param response The HTTP response object.
   * @param request The HTTP request object.
   * @note On Windows, an empty or root path returns the list of available drive letters.
   * @note On non-Windows, an empty path defaults to the filesystem root ("/").
   *
   * @api_examples{/api/browse?path=/home/user&type=directory| GET| null}
   */
  void browseDirectory(const resp_https_t &response, const req_https_t &request) {
    if (!authenticate(response, request)) {
      return;
    }

    print_req(request);

    try {
      const auto query_params = request->parse_query_string();

      std::string path_str;
      if (const auto path_it = query_params.find("path"); path_it != query_params.end()) {
        path_str = path_it->second;
      }

      std::string type_str = "any";
      if (const auto type_it = query_params.find("type"); type_it != query_params.end() && !type_it->second.empty()) {
        type_str = type_it->second;
      }

      nlohmann::json output_tree;

#ifdef _WIN32
      // On Windows with an empty or root path, return the list of available drive letters
      if (path_str.empty() || path_str == "/" || path_str == "\\") {
        output_tree["path"] = "";
        output_tree["parent"] = "";
        output_tree["entries"] = get_windows_drives();
        send_response(response, output_tree);
        return;
      }
#else
      // On non-Windows, default an empty path to the filesystem root
      if (path_str.empty()) {
        path_str = "/";
      }
#endif

      // Normalize the path
      fs::path dir_path = fs::weakly_canonical(fs::path(path_str));

      // If the path points to a file, use its parent directory
      std::error_code ec;
      if (fs::is_regular_file(dir_path, ec)) {
        dir_path = dir_path.parent_path();
      }

      // If the path doesn't exist, try the parent
      if (!fs::exists(dir_path, ec)) {
        dir_path = dir_path.parent_path();
      }

      if (!fs::is_directory(dir_path, ec)) {
        bad_request(response, request, "Path is not a directory");
        return;
      }

      output_tree["path"] = dir_path.string();

      // Determine the parent path for the "Up" navigation
      const fs::path parent = dir_path.parent_path();
#ifdef _WIN32
      // At a drive root (e.g., C:\) the parent equals itself; signal the drive list with an empty string
      output_tree["parent"] = (parent == dir_path) ? "" : parent.string();
#else
      output_tree["parent"] = parent.string();
#endif

      output_tree["entries"] = build_browse_entries(dir_path, type_str);
      send_response(response, output_tree);
    } catch (const fs::filesystem_error &e) {
      BOOST_LOG(warning) << "BrowseDirectory: "sv << e.what();
      bad_request(response, request, e.what());
    }
  }

  /**
   * @brief Start the HTTPS configuration server.
   */
  void start() {
    platf::set_thread_name("confighttp");
    const auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    const auto port_https = net::map_port(PORT_HTTPS);
    const auto address_family = net::af_from_enum_string(config::sunshine.address_family);

    https_server_t server {config::nvhttp.cert, config::nvhttp.pkey};

    // Helper to create page handler lambdas without repeating the signature
    auto page_handler = [](const char *file, bool require_auth = true, bool redirect_if_username = false) {
      return [file, require_auth, redirect_if_username](const resp_https_t &response, const req_https_t &request) {
        getPage(response, request, file, require_auth, redirect_if_username);
      };
    };

    // Default resource handlers
    const https_handler_t bad_request_handler = [](const resp_https_t &response, const req_https_t &request) {
      bad_request(response, request);
    };
    const https_handler_t not_found_handler = [](const resp_https_t &response, const req_https_t &request) {
      not_found(response, request);
    };

    // error by default
    server.default_resource["DELETE"] = bad_request_handler;
    server.default_resource["PATCH"] = bad_request_handler;
    server.default_resource["POST"] = bad_request_handler;
    server.default_resource["PUT"] = bad_request_handler;
    server.default_resource["GET"] = not_found_handler;

    // web pages
    server.resource["^/$"]["GET"] = page_handler("index.html");
    server.resource["^/apps/?$"]["GET"] = page_handler("apps.html");
    server.resource["^/config/?$"]["GET"] = page_handler("config.html");
    server.resource["^/featured/?$"]["GET"] = page_handler("featured.html");
    server.resource["^/logout/?$"]["GET"] = page_handler("logout.html", false);
    server.resource["^/password/?$"]["GET"] = page_handler("password.html");
    server.resource["^/pin/?$"]["GET"] = page_handler("pin.html");
    server.resource["^/troubleshooting/?$"]["GET"] = page_handler("troubleshooting.html");
    server.resource["^/welcome/?$"]["GET"] = page_handler("welcome.html", false, true);
    server.resource["^/login/?$"]["GET"] = getLoginPage;

    // rest api
    server.resource["^/api/login$"]["POST"] = login;
    server.resource["^/api/browse$"]["GET"] = browseDirectory;
    server.resource["^/api/csrf-token$"]["GET"] = getCSRFToken;
    server.resource["^/api/apps$"]["GET"] = getApps;
    server.resource["^/api/apps$"]["POST"] = saveApp;
    server.resource["^/api/apps/reorder$"]["POST"] = reorderApps;
    server.resource["^/api/apps/delete$"]["POST"] = deleteApp;
    server.resource["^/api/apps/launch$"]["POST"] = launchApp;
    server.resource["^/api/apps/close$"]["POST"] = closeApp;
    server.resource["^/api/clients/list$"]["GET"] = getClients;
    server.resource["^/api/clients/update$"]["POST"] = updateClient;
    server.resource["^/api/clients/unpair$"]["POST"] = unpair;
    server.resource["^/api/clients/unpair-all$"]["POST"] = unpairAll;
    server.resource["^/api/clients/disconnect$"]["POST"] = disconnect;
    server.resource["^/api/config$"]["GET"] = getConfig;
    server.resource["^/api/config$"]["POST"] = saveConfig;
    server.resource["^/api/configLocale$"]["GET"] = getLocale;
    server.resource["^/api/covers/([0-9]+)$"]["GET"] = getCover;
    server.resource["^/api/covers/upload$"]["POST"] = uploadCover;
    server.resource["^/api/logs$"]["GET"] = getLogs;
    server.resource["^/api/otp$"]["POST"] = getOTP;
    server.resource["^/api/password$"]["POST"] = savePassword;
    server.resource["^/api/pin$"]["POST"] = savePin;
    server.resource["^/api/quit$"]["POST"] = quit;
    server.resource["^/api/reset-display-device-persistence$"]["POST"] = resetDisplayDevicePersistence;
    server.resource["^/api/restart$"]["POST"] = restart;
    server.resource["^/api/vigembus/status$"]["GET"] = getViGEmBusStatus;
    server.resource["^/api/vigembus/install$"]["POST"] = installViGEmBus;

    // static/dynamic resources
    server.resource["^/images/apollo.ico$"]["GET"] = getFaviconImage;
    server.resource["^/images/logo-apollo-45.png$"]["GET"] = getLogoImage;
    server.resource["^/assets\\/.+$"]["GET"] = getAsset;

    server.config.reuse_address = true;
    server.config.address = net::get_bind_address(address_family);
    server.config.port = port_https;

    // Store bind address for logging, use "localhost" as fallback for wildcard addresses
    const auto bind_addr = server.config.address;
    const auto display_addr = config::sunshine.bind_address.empty() ? "localhost"sv : std::string_view {bind_addr};

    auto accept_and_run = [&](auto *server) {
      try {
        platf::set_thread_name("confighttp::tcp");
        server->start([&display_addr](const unsigned short port) {
          BOOST_LOG(info) << "Configuration UI available at [https://"sv << display_addr << ":" << port << "]";
        });
      } catch (boost::system::system_error &err) {
        // It's possible the exception gets thrown after calling server->stop() from a different thread
        if (shutdown_event->peek()) {
          return;
        }

        BOOST_LOG(fatal) << "Couldn't start Configuration HTTPS server on port ["sv << port_https << "]: "sv << err.what();
        shutdown_event->raise(true);
        return;
      }
    };
    std::jthread tcp {accept_and_run, &server};

    // Wait for any event
    shutdown_event->view();

    server.stop();

    tcp.join();
  }
}  // namespace confighttp
