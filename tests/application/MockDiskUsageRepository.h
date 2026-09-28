#pragma once

#include <gmock/gmock.h>

#include "IDiskUsageRepository.h"

class MockDiskUsageRepository : public IDiskUsageRepository
{
public:
    MOCK_METHOD(Result<void>, saveScan, (const std::filesystem::path& scanRoot, const std::vector<DiskUsageNode>& nodes), (override));
    MOCK_METHOD(Result<std::vector<DiskUsageNode>>, childrenOf, (const std::filesystem::path& folder), (const, override));
};
