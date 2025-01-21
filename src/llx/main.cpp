#include "llx.h"
#include <iostream>
#include <string>
#include <sstream>

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " \"<prompt>\"" << std::endl;
    std::cerr << "   or: " << program << " (enter multi-line input, terminate with two blank lines)" << std::endl;
    std::cerr << "Example: " << program << " \"What is the capital of France?\"" << std::endl;
}

int main(int argc, char** argv) {
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
    if (!client.connect()) {
        std::cerr << "Failed to connect to llxd. Make sure the daemon is running." << std::endl;
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