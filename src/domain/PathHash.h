#pragma once
#include <cstddef>
#include <filesystem>

// std::filesystem::path has no std::hash specialization (unlike std::string) -- only the free
// std::filesystem::hash_value() function, the same helper StandardFileSystemRepository already
// uses for path-keyed lookups -- so an unordered_map keyed by path needs this explicit hasher.
struct PathHash
{
    std::size_t operator()(const std::filesystem::path& path) const noexcept { return std::filesystem::hash_value(path); }
};
