#pragma once
#include <filesystem>
#include <unordered_map>

#include "PathHash.h"

using FolderSizesByPathMap = std::unordered_map<std::filesystem::path, std::uintmax_t, PathHash>;
