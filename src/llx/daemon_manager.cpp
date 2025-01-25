#include "daemon_manager.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <fcntl.h>
#include <fstream>

namespace fs = std::filesystem;

class DaemonManager::Impl {
public:
    bool is_running() const {
        int sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock < 0) return false;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/tmp/llx.sock", sizeof(addr.sun_path) - 1);

        bool running = (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0);
        close(sock);
        return running;
    }

    bool start_daemon(bool debug_mode) const {
        fs::path daemon_path = get_daemon_path();
        fs::path model_path = get_default_model_path();
        
        if (!fs::exists(daemon_path)) {
            std::cerr << "Could not find llxd executable at: " << daemon_path << std::endl;
            return false;
        }

        if (!fs::exists(model_path)) {
            std::cerr << "Could not find model at: " << model_path << std::endl;
            return false;
        }

        // Create log directory
        fs::path log_dir;
        if (const char* home = std::getenv("HOME")) {
            log_dir = fs::path(home) / ".cache" / "llx" / "logs";
        } else {
            log_dir = fs::current_path() / "logs";
        }
        
        try {
            fs::create_directories(log_dir);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create log directory: " << e.what() << std::endl;
            return false;
        }

        fs::path log_file = log_dir / "llxd.log";

        // Clear existing log file
        std::ofstream ofs(log_file, std::ofstream::out | std::ofstream::trunc);
        ofs.close();

        // Fork process
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Failed to fork process" << std::endl;
            return false;
        }

        if (pid == 0) {  // Child process
            // Detach from parent session
            if (setsid() < 0) {
                std::cerr << "Failed to create new session: " << strerror(errno) << std::endl;
                exit(1);
            }
            
            // Open log file
            int log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (log_fd < 0) {
                std::cerr << "Failed to open log file: " << strerror(errno) << std::endl;
                exit(1);
            }

            // Redirect stdout and stderr to log file
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);

            // Close all other file descriptors
            for (int i = 3; i < 1024; i++) {
                if (i != log_fd) {
                    close(i);
                }
            }

            // Prepare arguments
            std::string model_arg = "-m";
            std::vector<const char*> args = {
                daemon_path.c_str(),
                model_arg.c_str(),
                model_path.c_str(),
                "-d"  // Always enable debug mode during startup
            };
            
            args.push_back(nullptr);  // Null terminator

            // Execute daemon
            execv(daemon_path.c_str(), const_cast<char* const*>(args.data()));
            
            // If we get here, execv failed
            dprintf(STDERR_FILENO, "Failed to execute daemon: %s\n", strerror(errno));
            exit(1);
        }

        // Parent process
        // Wait for daemon to start
        const int MAX_RETRIES = 30;  // 30 * 200ms = 6 seconds
        int retries = MAX_RETRIES;
        while (retries-- > 0) {
            if (is_running()) {
                return true;
            }
            usleep(200000);  // 200ms
            
            // Check if the process is still running
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                // Process has exited
                if (WIFEXITED(status)) {
                    std::cerr << "Daemon process exited with status " << WEXITSTATUS(status) << std::endl;
                } else if (WIFSIGNALED(status)) {
                    std::cerr << "Daemon process killed by signal " << WTERMSIG(status) << std::endl;
                }
                break;
            }
        }

        // If daemon failed to start, check the log file
        try {
            std::ifstream log(log_file);
            std::string line;
            std::cerr << "Daemon failed to start. Log contents:" << std::endl;
            while (std::getline(log, line)) {
                std::cerr << line << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to read log file: " << e.what() << std::endl;
        }

        return false;
    }

    fs::path get_daemon_path() const {
        fs::path exe_dir;

#ifdef __APPLE__
        // Get executable path on macOS
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            exe_dir = fs::path(buffer.data()).parent_path();
        }
#else
        // Linux path using /proc/self/exe
        exe_dir = fs::read_symlink("/proc/self/exe").parent_path();
#endif

        // First check if llxd is in same directory as current executable
        if (!exe_dir.empty()) {
            fs::path daemon_path = exe_dir / "llxd";
            if (fs::exists(daemon_path)) {
                return daemon_path;
            }
        }

        // Then check in PATH
        const char* path = std::getenv("PATH");
        if (path) {
            std::string path_str(path);
            std::string delimiter = ":";
            size_t pos = 0;
            std::string token;
            
            while ((pos = path_str.find(delimiter)) != std::string::npos) {
                token = path_str.substr(0, pos);
                fs::path test_path = fs::path(token) / "llxd";
                if (fs::exists(test_path)) {
                    return test_path;
                }
                path_str.erase(0, pos + delimiter.length());
            }
            
            // Check last path component
            fs::path test_path = fs::path(path_str) / "llxd";
            if (fs::exists(test_path)) {
                return test_path;
            }
        }

        // Default to current directory
        return fs::current_path() / "llxd";
    }

    fs::path get_default_model_path() const {
        const char* home = std::getenv("HOME");
        if (!home) return fs::current_path() / "model.gguf";
        
        return fs::path(home) / ".cache" / "llx" / "Llama-3.2-3B-Instruct-Q4_K_M.gguf";
    }
};

DaemonManager::DaemonManager() : impl(std::make_unique<Impl>()) {}
DaemonManager::~DaemonManager() = default;

bool DaemonManager::is_running() const { return impl->is_running(); }
bool DaemonManager::start_daemon(bool debug_mode) const { return impl->start_daemon(debug_mode); }
fs::path DaemonManager::get_daemon_path() const { return impl->get_daemon_path(); }
fs::path DaemonManager::get_default_model_path() const { return impl->get_default_model_path(); } 