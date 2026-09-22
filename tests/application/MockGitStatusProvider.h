#pragma once

#include <gmock/gmock.h>

#include "IGitStatusProvider.h"

class MockGitStatusProvider : public IGitStatusProvider
{
public:
    MOCK_METHOD(Result<GitDirectoryStatus>, statusForDirectory, (const std::filesystem::path& directory), (const, override));
};
