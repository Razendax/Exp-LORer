#pragma once

#include <gmock/gmock.h>

#include "IFragmentationScanner.h"

class MockFragmentationScanner : public IFragmentationScanner
{
public:
    MOCK_METHOD(Result<std::vector<FileFragmentationInfo>>, scan,
                (const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
                 const std::function<void(std::size_t scanned, std::size_t totalCandidates, const std::filesystem::path& currentPath)>&
                     onProgress,
                 const std::atomic<bool>& cancelRequested),
                (const, override));
};
