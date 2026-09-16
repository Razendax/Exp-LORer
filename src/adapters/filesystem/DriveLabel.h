#pragma once

#include <cstdint>
#include <string>

// Pure, OS-call-free formatting of Explorer-style drive labels, split out from
// StandardFileSystemRepository specifically so it's unit-testable without touching real hardware
// (mirrors the WorkspaceLayoutTopology::visiblePanes() precedent for pure logic extracted from an
// otherwise-untestable adapter). See Architecture.md §14.15.
namespace DriveLabel
{
    // driveType is a Windows GetDriveTypeW() result (DRIVE_FIXED, DRIVE_REMOVABLE, DRIVE_CDROM,
    // DRIVE_REMOTE, ...). volumeLabel is the label from GetVolumeInformationW(), or empty if
    // unavailable/unset.
    std::wstring driveDisplayLabel(wchar_t letter, std::uint32_t driveType, const std::wstring& volumeLabel);
}
