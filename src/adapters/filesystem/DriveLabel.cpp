#include "DriveLabel.h"

namespace
{
    // Mirrors the Windows GetDriveTypeW() return values (winbase.h) so this file stays free of
    // any Windows.h include/OS call, keeping it plain and unit-testable.
    constexpr std::uint32_t kDriveRemovable = 2;
    constexpr std::uint32_t kDriveFixed = 3;
    constexpr std::uint32_t kDriveRemote = 4;
    constexpr std::uint32_t kDriveCdRom = 5;
    constexpr std::uint32_t kDriveRamDisk = 6;

    std::wstring typeFallbackLabel(std::uint32_t driveType)
    {
        switch (driveType)
        {
            case kDriveRemovable:
                return L"Removable Disk";
            case kDriveCdRom:
                return L"DVD Drive";
            case kDriveRemote:
                return L"Network Drive";
            case kDriveRamDisk:
                return L"RAM Disk";
            case kDriveFixed:
            default:
                return L"Local Disk";
        }
    }
}

namespace DriveLabel
{
    std::wstring driveDisplayLabel(wchar_t letter, std::uint32_t driveType, const std::wstring& volumeLabel)
    {
        const std::wstring suffix = std::wstring(L" (") + letter + L":)";
        if (!volumeLabel.empty())
        {
            return volumeLabel + suffix;
        }
        return typeFallbackLabel(driveType) + suffix;
    }
}
