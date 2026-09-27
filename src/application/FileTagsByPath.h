#pragma once
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "PathHash.h"
#include "Tag.h"

using TagsByPathMap = std::unordered_map<std::filesystem::path, std::vector<Tag>, PathHash>;
