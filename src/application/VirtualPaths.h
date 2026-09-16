#pragma once

#include <filesystem>

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
}
