#include "daemon_manager.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <curl/curl.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <fcntl.h>
#include "llama.h"
#include "common.h"

namespace fs = std::filesystem;

// Callback function for CURL to write downloaded data
static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* out = static_cast<std::ofstream*>(stream);
    out->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

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

    bool ensure_running(const std::optional<std::string>& model_id = std::nullopt) {
        if (is_running()) {
            return true;
        }

        std::string determined_model = determine_model_id(model_id);
        fs::path model_path = get_model_path(determined_model);

        if (!validate_model(determined_model)) {
            if (!download_model(determined_model)) {
                std::cerr << "Failed to download model: " << determined_model << std::endl;
                return false;
            }
        }

        return start_daemon(model_path.string());
    }

    std::string determine_model_id(const std::optional<std::string>& requested_model) const {
        if (requested_model && !requested_model->empty()) {
            return *requested_model;
        }
        return "bartowski/granite-3.1-2b-instruct-GGUF";  // Updated to include quantization
    }

    fs::path get_models_directory() const {
        const char* home = std::getenv("HOME");
        if (!home) return fs::current_path() / "models";
        return fs::path(home) / ".cache" / "llx" / "models";
    }

    fs::path get_model_path(const std::string& model_id) const {
        fs::path models_dir = get_models_directory();
        return models_dir / (model_id + ".gguf");
    }

    bool validate_model(const std::string& model_id) const {
        fs::path model_path = get_model_path(model_id);
        return fs::exists(model_path) && fs::file_size(model_path) > 0;
    }

    bool download_model(const std::string& model_id) const {
        try {
            // Get the actual repo and file name from the model ID (which may include quantization tag)
            auto [repo, file] = common_get_hf_file(model_id, "");  // Empty token for now, could be made configurable
            
            fs::path model_path = get_model_path(model_id);
            fs::create_directories(model_path.parent_path());

            // Use common_load_model_from_hf to download the model
            auto mparams = llama_model_default_params();
            llama_model* model = common_load_model_from_hf(
                repo,           // HF repo
                file,          // File within the repo
                model_path.string(),  // Local path to save to
                "",            // HF token (empty for now)
                mparams        // Model parameters
            );

            if (model == nullptr) {
                std::cerr << "Failed to download model from HuggingFace" << std::endl;
                return false;
            }

            // Free the model since we only needed it for downloading
            llama_model_free(model);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error downloading model: " << e.what() << std::endl;
            return false;
        }
    }

    bool start_daemon(const std::string& model_path) const {
        fs::path daemon_path = get_daemon_path();
        
        if (!fs::exists(daemon_path)) {
            std::cerr << "Could not find llxd executable at: " << daemon_path << std::endl;
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
            if (setsid() < 0) {
                std::cerr << "Failed to create new session: " << strerror(errno) << std::endl;
                exit(1);
            }
            
            int log_fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (log_fd < 0) {
                std::cerr << "Failed to open log file: " << strerror(errno) << std::endl;
                exit(1);
            }

            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);

            for (int i = 3; i < 1024; i++) {
                if (i != log_fd) {
                    close(i);
                }
            }

            std::string model_arg = "-m";
            std::vector<const char*> args = {
                daemon_path.c_str(),
                model_arg.c_str(),
                model_path.c_str(),
                "-d"  // Debug mode during startup
            };
            
            args.push_back(nullptr);

            execv(daemon_path.c_str(), const_cast<char* const*>(args.data()));
            
            dprintf(STDERR_FILENO, "Failed to execute daemon: %s\n", strerror(errno));
            exit(1);
        }

        // Parent process - wait for daemon to start
        const int MAX_RETRIES = 30;
        int retries = MAX_RETRIES;
        while (retries-- > 0) {
            if (is_running()) {
                return true;
            }
            usleep(200000);  // 200ms
            
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                if (WIFEXITED(status)) {
                    std::cerr << "Daemon process exited with status " << WEXITSTATUS(status) << std::endl;
                } else if (WIFSIGNALED(status)) {
                    std::cerr << "Daemon process killed by signal " << WTERMSIG(status) << std::endl;
                }
                break;
            }
        }

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
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            exe_dir = fs::path(buffer.data()).parent_path();
        }
#else
        exe_dir = fs::read_symlink("/proc/self/exe").parent_path();
#endif

        if (!exe_dir.empty()) {
            fs::path daemon_path = exe_dir / "llxd";
            if (fs::exists(daemon_path)) {
                return daemon_path;
            }
        }

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
            
            fs::path test_path = fs::path(path_str) / "llxd";
            if (fs::exists(test_path)) {
                return test_path;
            }
        }

        return fs::current_path() / "llxd";
    }
};

DaemonManager::DaemonManager() : impl(std::make_unique<Impl>()) {}
DaemonManager::~DaemonManager() = default;

bool DaemonManager::is_running() const { return impl->is_running(); }
bool DaemonManager::ensure_running(const std::optional<std::string>& model_id) { return impl->ensure_running(model_id); }
fs::path DaemonManager::get_daemon_path() const { return impl->get_daemon_path(); }
fs::path DaemonManager::get_default_model_path() const { return impl->get_model_path("TheBloke/Llama-3.2-3B-Instruct-GGUF"); } 