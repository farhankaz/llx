#include "llx.h"
#include "daemon_manager.h"
#include <iostream>
#include <string>
#include <sstream>

#ifndef LLX_VERSION
#define LLX_VERSION "unknown"
#endif

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_CODE    "\033[38;5;214m"  // Orange for inline code
#define COLOR_BLOCK   "\033[38;5;111m"  // Light blue for code blocks
#define COLOR_LANG    "\033[38;5;242m"  // Gray for language tags

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " \"<prompt>\"" << std::endl;
    std::cerr << "   or: " << program << " (enter multi-line input, terminate with two blank lines)" << std::endl;
    std::cerr << "   or: " << program << " --version" << std::endl;
    std::cerr << "   or: " << program << " --shutdown" << std::endl;
    std::cerr << "Example: " << program << " \"What is the capital of France?\"" << std::endl;
}

bool ensure_daemon_running() {
    DaemonManager daemon_manager;
    if (daemon_manager.is_running()) {
        return true;
    }
    return daemon_manager.ensure_running();
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

    // Ensure daemon is running before creating client
    if (!ensure_daemon_running()) {
        std::cerr << "Failed to start daemon" << std::endl;
        return 1;
    }

    llx client;
    if (!client.connect()) {
        std::cerr << "Failed to connect to llxd" << std::endl;
        return 1;
    }

    // Stream response to stdout
    bool success = client.query(prompt, [](const std::string& text) {
        static bool in_code_block = false;
        static bool in_backticks = false;
        static bool after_backticks = false;
        static std::string buffer;
        static std::string language_tag;

        // Process text character by character
        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            buffer += c;

            // Check for code block markers
            if (buffer.length() >= 3 && buffer.substr(buffer.length() - 3) == "```") {
                if (!in_code_block) {
                    // Start of code block
                    in_code_block = true;
                    after_backticks = true;
                    std::cout << "\n" << COLOR_BLOCK << buffer;
                } else {
                    // End of code block
                    in_code_block = false;
                    std::cout << buffer << COLOR_RESET << "\n";
                }
                buffer.clear();
                continue;
            }

            // Handle language tag after opening backticks
            if (after_backticks && !buffer.empty()) {
                if (buffer.find_first_not_of(" \t\n\r") != std::string::npos) {
                    language_tag += buffer;
                    if (buffer.find_first_of(" \t\n\r") != std::string::npos) {
                        // Output language tag if we have one
                        if (!language_tag.empty()) {
                            std::cout << COLOR_LANG << language_tag << COLOR_RESET << "\n";
                            language_tag.clear();
                        }
                        after_backticks = false;
                    }
                    buffer.clear();
                    continue;
                }
            }

            // Check for inline code
            if (!in_code_block && buffer.length() >= 1 && buffer.back() == '`') {
                if (!in_backticks) {
                    // Start of inline code
                    in_backticks = true;
                    std::cout << COLOR_CODE << buffer;
                } else {
                    // End of inline code
                    in_backticks = false;
                    std::cout << buffer << COLOR_RESET;
                }
                buffer.clear();
                continue;
            }

            // Output buffered text if it's getting too long or contains a newline
            if (buffer.length() > 32 || buffer.find('\n') != std::string::npos) {
                if (in_code_block) {
                    std::cout << COLOR_BLOCK << buffer << COLOR_RESET;
                } else if (in_backticks) {
                    std::cout << COLOR_CODE << buffer << COLOR_RESET;
                } else {
                    std::cout << buffer;
                }
                buffer.clear();
                std::cout << std::flush;
            }
        }

        // Output any remaining text
        if (!buffer.empty()) {
            if (in_code_block) {
                std::cout << COLOR_BLOCK << buffer << COLOR_RESET;
            } else if (in_backticks) {
                std::cout << COLOR_CODE << buffer << COLOR_RESET;
            } else {
                std::cout << buffer;
            }
            buffer.clear();
            std::cout << std::flush;
        }
    });

    if (!success) {
        std::cerr << "Failed to get response from llxd" << std::endl;
        return 1;
    }

    std::cout << std::endl;
    return 0;
} 