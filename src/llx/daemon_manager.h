#ifndef DAEMON_MANAGER_H
#define DAEMON_MANAGER_H

#include <string>
#include <memory>
#include <filesystem>
#include <optional>

class DaemonManager {
public:
    DaemonManager();
    ~DaemonManager();

    // Check if daemon is running by trying to connect to socket
    bool is_running() const;

    // Ensures daemon is running with the specified model (or default model if none specified)
    // Returns true if successful, false if there was an error
    bool ensure_running(const std::optional<std::string>& model_id = std::nullopt);

    // Get path to daemon executable
    std::filesystem::path get_daemon_path() const;

    // Get default model path
    std::filesystem::path get_default_model_path() const;

private:
    // Internal methods for model management
    std::string determine_model_id(const std::optional<std::string>& requested_model) const;
    std::filesystem::path get_models_directory() const;
    std::filesystem::path get_model_path(const std::string& model_id) const;
    bool download_model(const std::string& model_id) const;
    bool validate_model(const std::string& model_id) const;
    bool start_daemon(const std::string& model_path) const;

    class Impl;
    std::unique_ptr<Impl> impl;
};

#endif // DAEMON_MANAGER_H 