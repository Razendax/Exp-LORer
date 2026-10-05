#pragma once

#include <gmock/gmock.h>

#include "IDuplicateFileScanner.h"

class MockDuplicateFileScanner : public IDuplicateFileScanner
{
public:
    MOCK_METHOD(Result<std::vector<DuplicateFileGroup>>, scan,
                (const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
                 const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress,
                 const std::atomic<bool>& cancelRequested),
                (const, override));
};
