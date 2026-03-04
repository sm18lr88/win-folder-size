#pragma once
#include <cstdint>

namespace fs::ipc {

// Everything SDK3 named pipe names
// SDK3 builds pipe name as: "\\\\.\\.\\PIPE\\Everything IPC" + optional " (instance)"
// Everything 1.5a uses instance name "1.5a" -> pipe "Everything IPC (1.5a)"
// Unnamed instance (Everything 1.x) -> pipe "Everything IPC"
constexpr const wchar_t* EVERYTHING_PIPE_NAME = L"\\\\.\\PIPE\\Everything IPC";
constexpr const wchar_t* EVERYTHING_PIPE_NAME_1_5A = L"\\\\.\\PIPE\\Everything IPC (1.5a)";
// Legacy fallback — seen on some systems as service control pipe
constexpr const wchar_t* EVERYTHING_PIPE_NAME_SERVICE = L"\\\\.\\PIPE\\Everything Service (1.5a)";

// Everything SDK3 IPC message header
// All messages (request and response) use this 8-byte header
#pragma pack(push, 1)
struct Everything3Message {
    uint32_t code;  // Command or response code
    uint32_t size;  // Payload size in bytes (excludes this 8-byte header)
};
#pragma pack(pop)

// Command codes
constexpr uint32_t EVERYTHING3_COMMAND_GET_FOLDER_SIZE = 18;

// Sentinel value: folder not found / not indexed
constexpr uint64_t EVERYTHING_FOLDER_NOT_FOUND = UINT64_MAX;

// Response codes (from Everything SDK3)
constexpr uint32_t EVERYTHING3_RESPONSE_OK = 200;
constexpr uint32_t EVERYTHING3_RESPONSE_OK_MORE_DATA = 100;
constexpr uint32_t EVERYTHING3_RESPONSE_BAD_REQUEST = 400;
constexpr uint32_t EVERYTHING3_RESPONSE_NOT_FOUND = 404;
constexpr uint32_t EVERYTHING3_RESPONSE_OUT_OF_MEMORY = 500;
constexpr uint32_t EVERYTHING3_RESPONSE_INVALID_COMMAND = 501;

} // namespace fs::ipc
