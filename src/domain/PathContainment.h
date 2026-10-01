#pragma once

#include <filesystem>

// True if `candidate` is `root` itself or lexically nested under it. Never touches disk and never
// narrows a path via std::filesystem::path::string() (Architecture.md §6 -- uses PathUtf8::toUtf8()
// instead, safe for non-ASCII paths). Case-insensitive (see UnicodeCaseFold.h), matching Windows'
// own case-insensitive filesystem semantics.
bool isPathWithinOrEqual(const std::filesystem::path& candidate, const std::filesystem::path& root);

// True if `a` and `b` refer to the same path under Windows' case-insensitive filesystem semantics
// (see UnicodeCaseFold.h). Never touches disk and never narrows via
// std::filesystem::path::string(), same posture as isPathWithinOrEqual.
bool pathsEqualCaseInsensitive(const std::filesystem::path& a, const std::filesystem::path& b);

// Rewrites `candidate` -- which must satisfy isPathWithinOrEqual(candidate, oldRoot) -- so that its
// `oldRoot` prefix is replaced by `newRoot`, keeping the remaining components verbatim (e.g.
// rebasePath("C:/a/b/c", "C:/A", "C:/x") == "C:/x/b/c"). Used to re-point paths after a folder
// rename (Architecture.md §14.41). Lexical only, never touches disk; returns `candidate` unchanged
// if it isn't within `oldRoot`.
std::filesystem::path rebasePath(const std::filesystem::path& candidate, const std::filesystem::path& oldRoot,
                                 const std::filesystem::path& newRoot);
