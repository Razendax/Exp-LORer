#pragma once
#include <filesystem>
#include <string>

// Converts a path to UTF-8 without std::filesystem::path::string()'s dependency on the process's
// narrow ("ANSI") code page, which throws on Windows for characters outside that code page. Safe
// for text comparisons and error/log messages. Do not use for OS calls that open files — keep
// passing the path (or .wstring() on Windows) directly there.
namespace PathUtf8
{
    inline std::string toUtf8(const std::filesystem::path& path)
    {
        const std::u8string encoded = path.u8string();
        return std::string(encoded.begin(), encoded.end());
    }
}
