#pragma once
#include <cstdint>
#include <filesystem>
#include <unordered_map>

#include "PathHash.h"

using FragmentCountsByPathMap = std::unordered_map<std::filesystem::path, std::uint32_t, PathHash>;
