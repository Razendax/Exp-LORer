#pragma once

#include <gmock/gmock.h>

#include "IFileSystemRepository.h"

class MockFileSystemRepository : public IFileSystemRepository
{
public:
    MOCK_METHOD(Result<std::vector<FileNode>>, listDirectory, (const std::filesystem::path& directory), (const, override));
    MOCK_METHOD(Result<FileNode>, stat, (const std::filesystem::path& path), (const, override));
    MOCK_METHOD(Result<FileNode>, move, (const std::filesystem::path& source, const std::filesystem::path& destination), (override));
    MOCK_METHOD(Result<FileNode>, copy, (const std::filesystem::path& source, const std::filesystem::path& destination), (override));
    MOCK_METHOD(Result<void>, moveToTrash, (const std::filesystem::path& path), (override));
    MOCK_METHOD(Result<void>, deletePermanently, (const std::filesystem::path& path), (override));
    MOCK_METHOD(Result<void>, openWithDefaultApplication, (const std::filesystem::path& path), (override));
    MOCK_METHOD(Result<std::uint64_t>, computeFileHash, (const FileNode& file), (const, override));
};
