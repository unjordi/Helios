/**
 * @file src/process.h
 * @brief Declarations for the startup and shutdown of the apps started by a streaming Session.
 */
#pragma once

#ifndef __kernel_entry
  /**
   * @def __kernel_entry
   * @brief Macro for kernel entry.
   */
  #define __kernel_entry
#endif

#ifndef BOOST_PROCESS_VERSION
  #define BOOST_PROCESS_VERSION 1
#endif

// standard includes
#include <optional>
#include <unordered_map>

// lib includes
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/group.hpp>
#include <boost/process/v1/environment.hpp>
#include <boost/process/v1/search_path.hpp>
#include <boost/property_tree/ptree.hpp>
#include <nlohmann/json.hpp>

// local includes
#include "config.h"
#include "platform/common.h"
#include "rtsp.h"
#include "utility.h"

#ifdef _WIN32
  #include "platform/windows/virtual_display.h"
#endif

#define VIRTUAL_DISPLAY_UUID "8902CB19-674A-403D-A587-41B092E900BA"
#define FALLBACK_DESKTOP_UUID "EAAC6159-089A-46A9-9E24-6436885F6610"
#define REMOTE_INPUT_UUID "8CB5C136-DA67-4F99-B4A1-F9CD35005CF4"
#define TERMINATE_APP_UUID "E16CBE1B-295D-4632-9A76-EC4180C857D3"

/**
 * @def DEFAULT_APP_IMAGE_PATH
 * @brief Macro for DEFAULT APP IMAGE PATH.
 */
#define DEFAULT_APP_IMAGE_PATH SUNSHINE_ASSETS_DIR "/box.png"

namespace proc {
  /**
   * @brief Boost.Process pipe stream used for child-process I/O.
   */
  using file_t = util::safe_ptr_v2<FILE, int, fclose>;

#ifdef _WIN32
  extern VDISPLAY::DRIVER_STATUS vDisplayDriverStatus;
#endif

  /**
   * @brief Parsed command arguments used when launching a child process.
   */
  typedef config::prep_cmd_t cmd_t;

  /**
   * pre_cmds -- guaranteed to be executed unless any of the commands fail.
   * detached -- commands detached from Sunshine
   * cmd -- Runs indefinitely until:
   *    No session is running and a different set of commands it to be executed
   *    Command exits
   * working_dir -- the process working directory. This is required for some games to run properly.
   * cmd_output --
   *    empty    -- The output of the commands are appended to the output of sunshine
   *    "null"   -- The output of the commands are discarded
   *    filename -- The output of the commands are appended to filename
   */
  struct ctx_t {
    std::vector<cmd_t> prep_cmds;  ///< Prep cmds.
    std::vector<cmd_t> state_cmds;  ///< Commands run on client state changes (on-connect/on-disconnect).

    /**
     * Some applications, such as Steam, either exit quickly, or keep running indefinitely.
     *
     * Apps that launch normal child processes and terminate will be handled by the process
     * grouping logic (wait_all). However, apps that launch child processes indirectly or
     * into another process group (such as UWP apps) can only be handled by the auto-detach
     * heuristic which catches processes that exit 0 very quickly, but we won't have proper
     * process tracking for those.
     *
     * For cases where users just want to kick off a background process and never manage the
     * lifetime of that process, they can use detached commands for that.
     */
    std::vector<std::string> detached;

    std::string idx;  ///< Index of this app within the configured app list.
    std::string uuid;  ///< Stable UUID used to launch this app independently of its index.
    std::string name;  ///< Human-readable name for this item.
    std::string cmd;  ///< Command line used to launch the application.
    std::string working_dir;  ///< Working dir.
    std::string output;  ///< Captured output from the launched process.
    std::string image_path;  ///< Image path.
    std::string id;  ///< Stable identifier for the configured application.
    std::string gamepad;  ///< Gamepad type to emulate for this app.
    bool elevated;  ///< Whether the process should be launched elevated.
    bool auto_detach;  ///< Whether the process should detach automatically.
    bool wait_all;  ///< Whether Sunshine waits for all child processes.
    bool virtual_display;  ///< Whether the app is launched on a virtual display.
    bool virtual_display_primary;  ///< Whether the virtual display becomes the primary display.
    bool use_app_identity;  ///< Whether the app uses its own identity for display persistence.
    bool per_client_app_identity;  ///< Whether each client gets a separate app identity.
    bool allow_client_commands;  ///< Whether connected clients may run commands for this app.
    bool terminate_on_pause;  ///< Whether the app is terminated when the session is paused.
    int scale_factor;  ///< Display scale factor applied while this app is running.
    std::chrono::seconds exit_timeout;  ///< Exit timeout.
  };

  /**
   * @brief Tracks launched child processes and terminates them during shutdown.
   */
  class proc_t {
  public:
    KITTY_DEFAULT_CONSTR_MOVE_THROW(proc_t)

    std::string display_name;  ///< Display currently used by the running app.
    std::string initial_display;  ///< Display active before the app changed it.
    std::string mode_changed_display;  ///< Display whose mode was changed for the running app.
    bool initial_hdr = false;  ///< HDR state of the display before the app changed it.
    bool virtual_display = false;  ///< Whether the running app uses a virtual display.
    bool allow_client_commands = false;  ///< Whether the running app allows client-issued commands.

    /**
     * @brief Construct a process manager.
     *
     * @param env Environment used when launching processes.
     * @param apps Application launch contexts.
     */
    proc_t(
      boost::process::v1::environment &&env,
      std::vector<ctx_t> &&apps
    ):
        _env(std::move(env)),
        _apps(std::move(apps)) {
    }

    /**
     * @brief Enter the input-only pseudo app (no application is launched).
     */
    void launch_input_only();

    /**
     * @brief Launch the configured application process.
     *
     * @param app Application launch context to execute.
     * @param launch_session Launch session.
     * @return Process exit code or launch error status.
     */
    int execute(const ctx_t &app, std::shared_ptr<rtsp_stream::launch_session_t> launch_session);

    /**
     * @return `_app_id` if a process is running, otherwise returns `0`
     */
    int running();

    ~proc_t();

    /**
     * @brief Return the configured applications.
     *
     * @return Immutable application list owned by the process manager.
     */
    const std::vector<ctx_t> &get_apps() const;
    /**
     * @brief Return the configured applications.
     *
     * @return Mutable application list owned by the process manager.
     */
    std::vector<ctx_t> &get_apps();
    /**
     * @brief Get app image.
     *
     * @param app_id App ID.
     * @return Validated image path for the requested application.
     */
    std::string get_app_image(int app_id);
    /**
     * @brief Get last run app name.
     *
     * @return Name of the most recently launched application.
     */
    std::string get_last_run_app_name();
    /**
     * @brief Get the UUID of the currently running app.
     *
     * @return UUID of the running application, or an empty string when none is running.
     */
    std::string get_running_app_uuid();
    /**
     * @brief Get the environment used to launch applications.
     *
     * @return Copy of the process environment.
     */
    boost::process::v1::environment get_env();
    /**
     * @brief Resume a paused session (re-applies display and input state).
     */
    void resume();
    /**
     * @brief Pause the running session without terminating the app.
     */
    void pause();
    /**
     * @brief Terminate the launched application process.
     *
     * @param immediate Skip the graceful exit timeout.
     * @param needs_refresh Whether display/app state should be refreshed afterwards.
     */
    void terminate(bool immediate = false, bool needs_refresh = true);

  private:
    int _app_id = 0;
    std::string _app_name;

    boost::process::v1::environment _env;

    std::shared_ptr<rtsp_stream::launch_session_t> _launch_session;
    std::shared_ptr<config::input_t> _saved_input_config;

    std::vector<ctx_t> _apps;
    ctx_t _app;
    std::chrono::steady_clock::time_point _app_launch_time;

    // If no command associated with _app_id, yet it's still running
    bool placebo {};

    boost::process::v1::child _process;
    boost::process::v1::group _process_group;

    file_t _pipe;
    std::vector<cmd_t>::const_iterator _app_prep_it;
    std::vector<cmd_t>::const_iterator _app_prep_begin;
  };

  boost::filesystem::path
  find_working_directory(const std::string &cmd, const boost::process::v1::environment &env);

  /**
   * @brief Calculate a stable id based on name and image data
   * @return Tuple of id calculated without index (for use if no collision) and one with.
   *
   * @param app_name App name.
   * @param app_image_path App image path.
   * @param index Zero-based index of the item being addressed.
   */
  std::tuple<std::string, std::string> calculate_app_id(const std::string &app_name, std::string app_image_path, int index);

  bool check_valid_png(const std::filesystem::path &path);
  /**
   * @brief Validate app image path.
   *
   * @param app_image_path Candidate image path from the application configuration.
   * @return Existing PNG path, or the default application image when validation fails.
   */
  std::string validate_app_image_path(std::string app_image_path);
  /**
   * @brief Reload the app list from disk.
   *
   * @param file_name File name.
   * @param needs_terminate Whether a running app should be terminated before reloading.
   */
  void refresh(const std::string &file_name, bool needs_terminate = true);
  /**
   * @brief Migrate a legacy apps file to the current schema.
   *
   * @param fileTree_p Parsed apps file to migrate in place.
   * @param inputTree_p Incoming app definitions to merge, or null.
   */
  void migrate_apps(nlohmann::json *fileTree_p, nlohmann::json *inputTree_p);
  /**
   * @brief Parse serialized text into the corresponding runtime representation.
   *
   * @param file_name File name.
   * @return Parsed value or parse status.
   */
  std::optional<proc::proc_t> parse(const std::string &file_name);

  /**
   * @brief Initialize proc functions
   * @return Unique pointer to `deinit_t` to manage cleanup
   */
  std::unique_ptr<platf::deinit_t> init();

  /**
   * @brief Terminates all child processes in a process group.
   * @param proc The child process itself.
   * @param group The group of all children in the process tree.
   * @param exit_timeout The timeout to wait for the process group to gracefully exit.
   */
  void terminate_process_group(boost::process::v1::child &proc, boost::process::v1::group &group, std::chrono::seconds exit_timeout);

  extern proc_t proc;

  extern int input_only_app_id;
  extern std::string input_only_app_id_str;
  extern int terminate_app_id;
  extern std::string terminate_app_id_str;
}  // namespace proc

#ifdef BOOST_PROCESS_VERSION
  #undef BOOST_PROCESS_VERSION
#endif
