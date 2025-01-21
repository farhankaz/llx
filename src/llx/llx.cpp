#include "llx.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

class llx::Impl {
public:
    Impl() : socket_fd_(-1) {}

    ~Impl() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
        }
    }

    bool connect() {
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

        // Send the prompt
        ssize_t sent = send(socket_fd_, prompt.c_str(), prompt.length(), 0);
        if (sent != static_cast<ssize_t>(prompt.length())) {
            std::cerr << "Failed to send prompt" << std::endl;
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

private:
    int socket_fd_;
};

llx::llx() : impl(std::make_unique<Impl>()) {}
llx::~llx() = default;

bool llx::connect() {
    return impl->connect();
}

bool llx::query(const std::string& prompt, ResponseCallback callback) {
    return impl->query(prompt, callback);
} 