#pragma once

#include <os/log.h>
#include <sstream>
#include <string>

// Define OS_LOG_CATEGORY_INIT if not already defined (for macOS)
#ifndef OS_LOG_CATEGORY_INIT
#define OS_LOG_CATEGORY_INIT(name) name
#endif

// Macro to convert stream to string
#define STREAM_TO_STRING(x) (static_cast<std::ostringstream&>(std::ostringstream() << x).str())

// Logging macros that properly handle stream operations and format strings
#define LOG_INFO(format, ...) \
    do { \
        os_log_info(logger, format, ##__VA_ARGS__); \
    } while(0)

#define LOG_ERROR(format, ...) \
    do { \
        os_log_error(logger, format, ##__VA_ARGS__); \
    } while(0)

#define LOG_DEBUG(format, ...) \
    do { \
        os_log_debug(logger, format, ##__VA_ARGS__); \
    } while(0)

#define LOG_FAULT(format, ...) \
    do { \
        os_log_fault(logger, format, ##__VA_ARGS__); \
    } while(0)

// Debug logging macro that handles stream operations
#define DEBUG_LOG(x) \
    do { \
        if (debug_mode_) { \
            std::ostringstream ss; \
            ss << x; \
            std::string msg = ss.str(); \
            std::cout << "[DEBUG] " << msg << std::endl; \
            os_log_debug(logger, "%{public}s", msg.c_str()); \
        } \
    } while(0) 