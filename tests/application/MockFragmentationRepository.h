#pragma once

#include <gmock/gmock.h>

#include "IFragmentationRepository.h"

class MockFragmentationRepository : public IFragmentationRepository
{
public:
    MOCK_METHOD(Result<void>, saveScan,
                (const std::filesystem::path& scanRoot, std::uintmax_t minFileSizeBytes,
                 const std::vector<FileFragmentationInfo>& files),
                (override));
    MOCK_METHOD(Result<std::vector<FileFragmentationInfo>>, filesUnder, (const std::filesystem::path& folder), (const, override));
};
