#ifndef LLX_H
#define LLX_H

#include <string>
#include <memory>
#include <functional>

class llx {
public:
    // Callback type for receiving streamed responses
    using ResponseCallback = std::function<void(const std::string&)>;

    llx();
    ~llx();

    // Connect to the daemon
    bool connect();

    // Send a prompt and receive response
    bool query(const std::string& prompt, ResponseCallback callback);

    // Send shutdown command to daemon
    bool shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

#endif // LLX_H 