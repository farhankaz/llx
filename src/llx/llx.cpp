#include "llx.h"
#include "daemon_manager.h"
#include "../llxd/protocol.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

class llx::Impl {
public:
    Impl() : socket_fd_(-1), daemon_manager_() {}

    ~Impl() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
        }
    }

    bool connect(bool auto_start, bool debug_mode) {
        // Check if daemon is running
        if (!daemon_manager_.is_running()) {
            if (!auto_start) {
                std::cerr << "Daemon not running and auto-start disabled" << std::endl;
                return false;
            }

            // Try to start daemon
            if (!daemon_manager_.start_daemon(debug_mode)) {
                return false;
            }
        }

        socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/tmp/llx.sock", sizeof(addr.sun_path) - 1);

        if (::connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Failed to connect to daemon" << std::endl;
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }

        return true;
    }

    bool query(const std::string& prompt, ResponseCallback callback) {
        if (socket_fd_ < 0) {
            std::cerr << "Not connected to daemon" << std::endl;
            return false;
        }

        // Prepare and send message header
        llxd_protocol::MessageHeader header;
        header.type = llxd_protocol::MessageType::PROMPT;
        header.payload_size = htonl(prompt.length());  // Convert to network byte order

        if (!send_all(&header, sizeof(header))) {
            return false;
        }

        // Send prompt
        if (!send_all(prompt.c_str(), prompt.length())) {
            return false;
        }

        // Read response in chunks
        char buffer[4096];
        while (true) {
            ssize_t n = read(socket_fd_, buffer, sizeof(buffer) - 1);
            if (n <= 0) {
                break;
            }
            buffer[n] = '\0';
            callback(std::string(buffer, n));
        }

        return true;
    }

    bool shutdown() {
        if (socket_fd_ < 0) {
            std::cerr << "Not connected to daemon" << std::endl;
            return false;
        }

        // Prepare and send message header
        llxd_protocol::MessageHeader header;
        header.type = llxd_protocol::MessageType::CONTROL;
        header.payload_size = htonl(sizeof(llxd_protocol::ControlCommand));

        if (!send_all(&header, sizeof(header))) {
            return false;
        }

        // Send shutdown command
        llxd_protocol::ControlCommand cmd = llxd_protocol::ControlCommand::SHUTDOWN;
        if (!send_all(&cmd, sizeof(cmd))) {
            return false;
        }

        // Read confirmation response
        char buffer[4096];
        ssize_t n = read(socket_fd_, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            std::cout << buffer;
        }

        // Close socket
        close(socket_fd_);
        socket_fd_ = -1;

        return true;
    }

private:
    bool send_all(const void* data, size_t len) {
        const char* ptr = static_cast<const char*>(data);
        size_t remaining = len;

        while (remaining > 0) {
            ssize_t sent = send(socket_fd_, ptr, remaining, MSG_NOSIGNAL);
            if (sent <= 0) {
                std::cerr << "Failed to send data" << std::endl;
                return false;
            }
            ptr += sent;
            remaining -= sent;
        }
        return true;
    }

    int socket_fd_;
    DaemonManager daemon_manager_;
};

llx::llx() : impl(std::make_unique<Impl>()) {}
llx::~llx() = default;

bool llx::connect(bool auto_start, bool debug_mode) {
    return impl->connect(auto_start, debug_mode);
}

bool llx::query(const std::string& prompt, ResponseCallback callback) {
    return impl->query(prompt, callback);
}

bool llx::shutdown() {
    return impl->shutdown();
} 