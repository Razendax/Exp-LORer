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
