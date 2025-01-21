#include "llxd.h"
#include <signal.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <atomic>
#include <filesystem>
#include <cstdlib>
#include <curl/curl.h>
#include <fstream>

#ifndef LLX_VERSION
#define LLX_VERSION "unknown"
#endif

namespace fs = std::filesystem;

// Callback function to write downloaded data to file
static size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* file = static_cast<std::ofstream*>(stream);
    file->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

// Function to download file from URL
bool download_file(const std::string& url, const std::string& output_path) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ofstream file(output_path, std::ios::binary);
    if (!file) return false;

    std::cout << "Downloading Llama-3.2-3B model... This may take a while." << std::endl;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    file.close();

    return res == CURLE_OK;
}

static llxd* g_daemon = nullptr;
static std::atomic<bool> g_running(true);

void signal_handler(int sig) {
    if (g_daemon) {
        std::cout << "\nReceived signal " << sig << ", shutting down..." << std::endl;
        g_daemon->stop();
        g_running = false;  // Signal the main loop to exit
    }
}

int main(int argc, char** argv) {
    // Check for version flag first
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--version") {
            std::cout << "llxd version " << LLX_VERSION << std::endl;
            return 0;
        }
    }

    std::string model_path;
    bool debug_mode = false;
    const std::string DEFAULT_MODEL = "Llama-3.2-3B-Instruct-Q4_K_M.gguf";
    const std::string MODEL_URL = "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-m" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "-d") {
            debug_mode = true;
        }
    }

    // If no model path provided, use cached model
    if (model_path.empty()) {
        // Get home directory
        const char* home = std::getenv("HOME");
        if (!home) {
            std::cerr << "Could not determine home directory" << std::endl;
            return 1;
        }

        // Create cache directory if it doesn't exist
        fs::path cache_dir = fs::path(home) / ".cache" / "llx";
        try {
            fs::create_directories(cache_dir);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create cache directory: " << e.what() << std::endl;
            return 1;
        }

        // Check if default model exists
        fs::path model_file = cache_dir / DEFAULT_MODEL;
        if (!fs::exists(model_file)) {
            // Download the model
            if (!download_file(MODEL_URL, model_file.string())) {
                std::cerr << "Failed to download model" << std::endl;
                return 1;
            }
            std::cout << "Model download complete." << std::endl;
        }

        model_path = model_file.string();
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    // Create and start daemon
    llxd daemon(model_path, debug_mode);
    g_daemon = &daemon;

    if (!daemon.start()) {
        std::cerr << "Failed to start daemon" << std::endl;
        return 1;
    }

    // Wait for signals, but check g_running flag
    while (g_running) {
        sleep(1);  // Sleep for short intervals instead of indefinite pause
    }

    std::cout << "Cleanup complete, exiting." << std::endl;
    return 0;
} 