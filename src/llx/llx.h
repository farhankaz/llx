#ifndef LLX_H
#define LLX_H

#include <string>
#include <functional>
#include <memory>

class llx {
public:
    // Callback type for receiving streamed responses
    using ResponseCallback = std::function<void(const std::string& text)>;

    llx();
    ~llx();

    // Connect to the llxd daemon, optionally auto-starting it if not running
    bool connect(bool auto_start = true, bool debug_mode = false);

    // Send a prompt and receive streamed response
    bool query(const std::string& prompt, ResponseCallback callback);

    // Send shutdown command to daemon
    bool shutdown();

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

#endif // LLX_H 