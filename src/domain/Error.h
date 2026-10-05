#pragma once

#include <string>
#include <string_view>
#include <utility>

enum class ErrorCode
{
    InvalidArgument,
    NotFound,
    AlreadyExists,
    IoError,
    Unknown,
    Cancelled,             // User cancelled via the progress notification's Cancel button.
    InsufficientDiskSpace, // Destination volume doesn't have enough free space for the transfer.
};

// Stable, path-free name for an ErrorCode, safe to put in a log line (Architecture.md §14.23) —
// unlike Error::message, which is often built from a third-party exception/driver string that can
// embed a filesystem path.
constexpr std::string_view toLogString(ErrorCode code) noexcept
{
    switch (code)
    {
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::NotFound: return "NotFound";
        case ErrorCode::AlreadyExists: return "AlreadyExists";
        case ErrorCode::IoError: return "IoError";
        case ErrorCode::Unknown: return "Unknown";
        case ErrorCode::Cancelled: return "Cancelled";
        case ErrorCode::InsufficientDiskSpace: return "InsufficientDiskSpace";
    }
    return "Unknown";
}

struct Error
{
    ErrorCode code;
    std::string message;

    Error(ErrorCode code, std::string message)
        : code(code)
        , message(std::move(message))
    {
    }
};
