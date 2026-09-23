#pragma once

#include <filesystem>

// True if `candidate` is `root` itself or lexically nested under it. Never touches disk and never
// narrows a path to std::string (Architecture.md §6) -- operates on std::filesystem::path objects
// directly via lexically_relative, so it is safe for non-ASCII paths.
bool isPathWithinOrEqual(const std::filesystem::path& candidate, const std::filesystem::path& root);
