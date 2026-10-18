#pragma once

#include <filesystem>

#include "ArchiveExtensions.h"

// Sentinel paths for virtual navigation locations that don't correspond to real filesystem
// entries (Architecture.md §14.15). Plain std::filesystem::path, same posture as NativeTypes.h —
// no Qt/OS dependency in Application.
namespace VirtualPaths
{
    // "This PC": lists local/remote drives plus quick-access folders (Downloads, Documents,
    // Pictures, Videos, Music, Desktop). A trailing ':' is illegal in a real Windows path
    // component, so this literal can never collide with an actual file or folder. Handled
    // entirely inside StandardFileSystemRepository (listDirectory/stat special-case it); no
    // IFileSystemRepository signature change was needed since both already take/return generic
    // std::filesystem::path.
    inline const std::filesystem::path ThisPC{L"this-pc:"};

    // True if `path` is usable as a real scan root for a feature like Disk Usage/Fragmentation/
    // Monitoring: not empty, not the ThisPC sentinel above, and not inside an archive (per
    // ArchiveExtensions::archiveAncestorInPath). Lexical only, no I/O. Shared by FolderSizeWidget,
    // FragmentationWidget, and MonitoringPanelWidget's "can I start this action" button-enable
    // logic.
    inline bool isValidScanRoot(const std::filesystem::path& path)
    {
        return !path.empty() && path != ThisPC && !ArchiveExtensions::archiveAncestorInPath(path);
    }
}
