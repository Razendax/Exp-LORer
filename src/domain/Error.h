#pragma once

#include <string>
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
