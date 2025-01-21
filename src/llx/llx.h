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

    // Connect to the llxd daemon
    bool connect();

    // Send a prompt and receive streamed response
    bool query(const std::string& prompt, ResponseCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

#endif // LLX_H 