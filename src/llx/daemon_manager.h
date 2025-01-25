#ifndef DAEMON_MANAGER_H
#define DAEMON_MANAGER_H

#include <string>
#include <memory>
#include <filesystem>

class DaemonManager {
public:
    DaemonManager();
    ~DaemonManager();

    // Check if daemon is running by trying to connect to socket
    bool is_running() const;

    // Start daemon as background process
    bool start_daemon(bool debug_mode = false) const;

    // Get path to daemon executable
    std::filesystem::path get_daemon_path() const;

    // Get default model path
    std::filesystem::path get_default_model_path() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

#endif // DAEMON_MANAGER_H 