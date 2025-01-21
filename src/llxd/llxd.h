#ifndef LLXD_H
#define LLXD_H

#include <string>
#include <memory>

class llxd {
public:
    llxd(const std::string& model_path, bool debug_mode = false);
    ~llxd();

    // Start the daemon
    bool start();

    // Stop the daemon
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

#endif // LLXD_H
