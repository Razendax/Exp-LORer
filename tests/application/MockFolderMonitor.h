#pragma once

#include <gmock/gmock.h>

#include "IFolderMonitor.h"

class MockFolderMonitor : public IFolderMonitor
{
public:
    MOCK_METHOD(void, startMonitoring,
                (const std::filesystem::path& rootDirectory, std::function<void(const std::vector<FolderChangeEvent>&)> onChanged,
                 std::function<void(Result<FolderMonitorId>)> onStarted),
                (override));
    MOCK_METHOD(void, stopMonitoring, (FolderMonitorId id), (override));
};
