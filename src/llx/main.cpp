#include "llx.h"
#include <iostream>
#include <string>
#include <sstream>

#ifndef LLX_VERSION
#define LLX_VERSION "unknown"
#endif

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " \"<prompt>\"" << std::endl;
    std::cerr << "   or: " << program << " (enter multi-line input, terminate with two blank lines)" << std::endl;
    std::cerr << "   or: " << program << " --version" << std::endl;
    std::cerr << "   or: " << program << " --shutdown" << std::endl;
    std::cerr << "Example: " << program << " \"What is the capital of France?\"" << std::endl;
}

int main(int argc, char** argv) {
    // Handle version flag
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "llx version " << LLX_VERSION << std::endl;
        return 0;
    }

    // Handle shutdown flag
    if (argc == 2 && std::string(argv[1]) == "--shutdown") {
        llx client;
        if (!client.connect()) {
            std::cerr << "Failed to connect to llxd. Make sure the daemon is running." << std::endl;
            return 1;
        }

        if (!client.shutdown()) {
            std::cerr << "Failed to shutdown llxd daemon" << std::endl;
            return 1;
        }
        return 0;
    }

    // Validate no flags with prompt
    if (argc >= 2 && argv[1][0] == '-') {
        std::cerr << "Error: Unknown flag '" << argv[1] << "'" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    std::string prompt;

    if (argc == 1) {
        // Multi-line input mode
        std::cout << "Enter your prompt (terminate with two blank lines):" << std::endl;
        std::string line;
        std::stringstream input;
        bool last_line_empty = false;

        while (std::getline(std::cin, line)) {
            if (line.empty()) {
                if (last_line_empty) {
                    break;  // Two consecutive empty lines
                }
                last_line_empty = true;
            } else {
                last_line_empty = false;
            }
            input << line << "\n";
        }
        prompt = input.str();
        
        // Remove the last two blank lines
        while (!prompt.empty() && prompt.back() == '\n') {
            prompt.pop_back();
        }
    } else if (argc == 2) {
        prompt = argv[1];
    } else {
        print_usage(argv[0]);
        return 1;
    }

    if (prompt.empty()) {
        std::cerr << "Error: Empty prompt" << std::endl;
        return 1;
    }

    llx client;
    if (!client.connect(true, false)) {  // Enable auto-start, disable debug mode
        std::cerr << "Failed to connect to llxd and auto-start failed" << std::endl;
        return 1;
    }

    // Stream response to stdout
    bool success = client.query(prompt, [](const std::string& text) {
        std::cout << text << std::flush;
    });

    if (!success) {
        std::cerr << "Failed to get response from llxd" << std::endl;
        return 1;
    }

    std::cout << std::endl;
    return 0;
} 