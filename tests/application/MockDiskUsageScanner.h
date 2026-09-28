#pragma once

#include <gmock/gmock.h>

#include "IDiskUsageScanner.h"

class MockDiskUsageScanner : public IDiskUsageScanner
{
public:
    MOCK_METHOD(Result<std::vector<DiskUsageNode>>, scan,
                (const std::filesystem::path& root, const std::function<void()>& onProgress), (const, override));
};
