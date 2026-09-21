#pragma once

#include <gmock/gmock.h>

#include "IFileSystemRepository.h"

class MockFileSystemRepository : public IFileSystemRepository
{
public:
    MOCK_METHOD(Result<std::vector<FileNode>>, listDirectory, (const std::filesystem::path& directory), (const, override));
    MOCK_METHOD(Result<std::vector<FileNode>>, listDirectoryRecursive, (const std::filesystem::path& root), (const, override));
    MOCK_METHOD(Result<FileNode>, stat, (const std::filesystem::path& path), (const, override));
    MOCK_METHOD(Result<FileNode>, move, (const std::filesystem::path& source, const std::filesystem::path& destination), (override));
    MOCK_METHOD(Result<FileNode>, copy, (const std::filesystem::path& source, const std::filesystem::path& destination), (override));
    MOCK_METHOD(Result<void>, moveToTrash, (const std::filesystem::path& path), (override));
    MOCK_METHOD(Result<void>, deletePermanently, (const std::filesystem::path& path), (override));
    MOCK_METHOD(Result<void>, openWithDefaultApplication, (const std::filesystem::path& path), (override));
    MOCK_METHOD(Result<std::uint64_t>, computeFileHash, (const FileNode& file), (const, override));
    MOCK_METHOD(Result<void>, createDirectory, (const std::filesystem::path& directory), (override));
    MOCK_METHOD(Result<FileNode>, createFileFromTemplate,
                (const std::filesystem::path& destinationFile, const std::optional<std::filesystem::path>& templateFile),
                (override));
    MOCK_METHOD(Result<void>, showProperties, (const std::filesystem::path& path, NativeWindowHandle ownerWindow), (override));
    MOCK_METHOD(Result<std::vector<std::byte>>, readFilePrefix, (const std::filesystem::path& path, std::size_t maxBytes),
                (const, override));
    MOCK_METHOD(Result<void>, extractArchive,
                (const std::filesystem::path& archiveFile, const std::filesystem::path& destinationDirectory), (override));
    MOCK_METHOD(Result<std::filesystem::path>, materializeForReading, (const std::filesystem::path& path), (const, override));
};
