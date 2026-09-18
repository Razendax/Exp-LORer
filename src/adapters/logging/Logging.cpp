#include "Logging.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#if !defined(NDEBUG)
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace
{
    constexpr const char* kLoggerName = "exp-lorer";
    constexpr std::size_t kMaxFileSize = 5 * 1024 * 1024;
    constexpr std::size_t kMaxFiles = 3;

    std::shared_ptr<spdlog::logger> g_logger;

    spdlog::level::level_enum toSpdlogLevel(Logging::Level level)
    {
        switch (level)
        {
            case Logging::Level::Debug:
                return spdlog::level::debug;
            case Logging::Level::Info:
                return spdlog::level::info;
            case Logging::Level::Warning:
                return spdlog::level::warn;
            case Logging::Level::Error:
                return spdlog::level::err;
        }
        return spdlog::level::info;
    }
}

void Logging::init(const std::filesystem::path& logDirectory)
{
    if (g_logger)
    {
        return;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(logDirectory, errorCode);

    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        (logDirectory / "exp-lorer.log").string(), kMaxFileSize, kMaxFiles));

#if !defined(NDEBUG)
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif

    g_logger = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());
    g_logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
    g_logger->set_level(spdlog::level::debug);
    g_logger->flush_on(spdlog::level::warn);
}

void Logging::shutdown()
{
    if (!g_logger)
    {
        return;
    }

    g_logger->flush();
    g_logger.reset();
}

void Logging::log(Level level, std::string_view message)
{
    if (!g_logger)
    {
        return;
    }

    g_logger->log(toSpdlogLevel(level), std::string(message));
}

void Logging::debug(std::string_view message)
{
    log(Level::Debug, message);
}

void Logging::info(std::string_view message)
{
    log(Level::Info, message);
}

void Logging::warn(std::string_view message)
{
    log(Level::Warning, message);
}

void Logging::error(std::string_view message)
{
    log(Level::Error, message);
}
