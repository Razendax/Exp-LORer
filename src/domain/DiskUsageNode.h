#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Plain data describing one file or directory discovered during a disk-usage scan (Architecture.md
// §14.39). Deliberately not FileNode -- FileNode::size() is always an item's own byte size, never a
// recursive aggregate, and overloading that meaning would be a landmine for every existing FileNode
// caller. A directory's size here is the recursive aggregate of its whole subtree; a file's is its
// own byte size, same as FileNode's.
struct DiskUsageNode
{
    std::filesystem::path path;
    std::string name;
    std::uintmax_t size = 0;
    bool isDirectory = false;
};

inline bool operator==(const DiskUsageNode& lhs, const DiskUsageNode& rhs) noexcept
{
    return lhs.path == rhs.path && lhs.name == rhs.name && lhs.size == rhs.size && lhs.isDirectory == rhs.isDirectory;
}
