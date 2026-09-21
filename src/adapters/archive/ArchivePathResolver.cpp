#include "ArchivePathResolver.h"

#include <system_error>

#include "ArchiveExtensions.h"

namespace
{
    namespace fs = std::filesystem;
}

std::optional<ArchivePathResolver::Resolution> ArchivePathResolver::resolve(const std::filesystem::path& path)
{
    std::optional<fs::path> archiveFile;

    fs::path current = path;
    for (;;)
    {
        if (ArchiveExtensions::isArchiveExtension(current))
        {
            std::error_code ec;
            if (fs::is_regular_file(current, ec))
            {
                archiveFile = current;
            }
        }

        fs::path parent = current.parent_path();
        if (parent.empty() || parent == current)
        {
            break;
        }
        current = parent;
    }

    if (!archiveFile)
    {
        return std::nullopt;
    }

    fs::path relative = path.lexically_relative(*archiveFile);
    if (relative == fs::path("."))
    {
        relative.clear();
    }

    return Resolution{ *archiveFile, relative };
}
