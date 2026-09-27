#pragma once

#include <filesystem>

// True if `candidate` is `root` itself or lexically nested under it. Never touches disk and never
// narrows a path via std::filesystem::path::string() (Architecture.md §6 -- uses PathUtf8::toUtf8()
// instead, safe for non-ASCII paths). Case-insensitive (see UnicodeCaseFold.h), matching Windows'
// own case-insensitive filesystem semantics.
bool isPathWithinOrEqual(const std::filesystem::path& candidate, const std::filesystem::path& root);
