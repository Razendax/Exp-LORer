#include "BinaryContentDetector.h"

#include <algorithm>

bool BinaryContentDetector::containsNulByte(const std::vector<std::byte>& bytes)
{
    return std::any_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{ 0 }; });
}
