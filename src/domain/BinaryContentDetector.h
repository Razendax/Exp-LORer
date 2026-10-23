#pragma once

#include <cstddef>
#include <vector>

// Shared NUL-byte heuristic for "is this a binary file" (Architecture.md §14.25), originally
// inlined in FilePreviewUseCase::generatePreview and extracted here once Content Search
// (§14.104) became a second call site.
namespace BinaryContentDetector
{
    // True if bytes contains a NUL byte anywhere -- the heuristic both the text preview and
    // Content Search's directory scan use to skip binary files.
    bool containsNulByte(const std::vector<std::byte>& bytes);
}
