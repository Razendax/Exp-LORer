#pragma once

#include <gmock/gmock.h>

#include "IDiskUsageScanner.h"

class MockDiskUsageScanner : public IDiskUsageScanner
{
public:
    MOCK_METHOD(Result<std::vector<DiskUsageNode>>, scan,
                (const std::filesystem::path& root,
                 const std::function<void(std::size_t scanned, const std::filesystem::path& currentPath)>& onProgress,
                 const std::atomic<bool>& cancelRequested),
                (const, override));
};
