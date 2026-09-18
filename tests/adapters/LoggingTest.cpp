#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "Logging.h"

namespace
{
    namespace fs = std::filesystem;

    class LoggingTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_tempDir = fs::temp_directory_path() / "explorer_logging_test";
            fs::remove_all(m_tempDir);
            fs::create_directories(m_tempDir);
        }

        void TearDown() override
        {
            Logging::shutdown();
            fs::remove_all(m_tempDir);
        }

        fs::path m_tempDir;
    };

    std::string readLogFile(const fs::path& logDir)
    {
        std::ifstream file(logDir / "exp-lorer.log");
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
}

TEST_F(LoggingTest, InitCreatesLogFile)
{
    Logging::init(m_tempDir);
    Logging::shutdown();

    EXPECT_TRUE(fs::exists(m_tempDir / "exp-lorer.log"));
}

TEST_F(LoggingTest, ErrorMessageIsFlushedToFile)
{
    Logging::init(m_tempDir);
    Logging::error("boom");
    Logging::shutdown();

    const std::string contents = readLogFile(m_tempDir);
    EXPECT_NE(contents.find("boom"), std::string::npos);
    EXPECT_NE(contents.find("[error]"), std::string::npos);
}
