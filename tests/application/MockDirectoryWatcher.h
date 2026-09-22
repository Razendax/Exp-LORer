#pragma once

#include <gmock/gmock.h>

#include "IDirectoryWatcher.h"

class MockDirectoryWatcher : public IDirectoryWatcher
{
public:
    MOCK_METHOD(Result<DirectoryWatchId>, watch, (const std::filesystem::path& directory, std::function<void()> onChanged), (override));
    MOCK_METHOD(void, unwatch, (DirectoryWatchId id), (override));
};
