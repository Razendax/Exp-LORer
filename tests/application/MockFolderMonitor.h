#pragma once

#include <gmock/gmock.h>

#include "IFolderMonitor.h"

class MockFolderMonitor : public IFolderMonitor
{
public:
    MOCK_METHOD(Result<FolderMonitorId>, startMonitoring,
                (const std::filesystem::path& rootDirectory, std::function<void(const std::vector<FolderChangeEvent>&)> onChanged),
                (override));
    MOCK_METHOD(void, stopMonitoring, (FolderMonitorId id), (override));
};
