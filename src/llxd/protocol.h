#ifndef LLXD_PROTOCOL_H
#define LLXD_PROTOCOL_H

#include <cstdint>
#include <string>

namespace llxd_protocol {

// Message types
enum class MessageType : uint8_t {
    PROMPT = 0,     // Text generation prompt
    CONTROL = 1     // Control command
};

// Control command types
enum class ControlCommand : uint8_t {
    SHUTDOWN = 0
};

// Message header structure
struct MessageHeader {
    MessageType type;
    uint32_t payload_size;
};

} // namespace llxd_protocol

#endif // LLXD_PROTOCOL_H 