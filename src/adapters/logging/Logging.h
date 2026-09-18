#pragma once

#include <filesystem>
#include <string_view>

// Thin wrapper over spdlog (Architecture.md §6/§14.22) — this module is the only place an spdlog
// header is included, so the rest of the codebase (including Domain/Application) never depends on
// it directly.
namespace Logging
{
    enum class Level
    {
        Debug,
        Info,
        Warning,
        Error
    };

    // Idempotent-safe single call from main() before anything else logs. Creates logDirectory if
    // needed, sets up a rotating file sink (5 MB x 3 files) there, plus a console sink when built
    // without NDEBUG. Pattern includes a timestamp and level on every line.
    void init(const std::filesystem::path& logDirectory);

    // Flushes and releases the logger. Call once, right before process exit.
    void shutdown();

    void log(Level level, std::string_view message);
    void debug(std::string_view message);
    void info(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);
}
