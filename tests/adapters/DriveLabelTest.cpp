#include <gtest/gtest.h>

#include "DriveLabel.h"

namespace
{
    // Mirrors the Windows GetDriveTypeW() values used by DriveLabel.cpp's own local constants.
    constexpr std::uint32_t kDriveRemovable = 2;
    constexpr std::uint32_t kDriveFixed = 3;
    constexpr std::uint32_t kDriveRemote = 4;
    constexpr std::uint32_t kDriveCdRom = 5;
}

TEST(DriveLabel, FixedDriveWithVolumeLabelUsesVolumeLabel)
{
    EXPECT_EQ(DriveLabel::driveDisplayLabel(L'C', kDriveFixed, L"System"), L"System (C:)");
}

TEST(DriveLabel, FixedDriveWithoutVolumeLabelFallsBackToLocalDisk)
{
    EXPECT_EQ(DriveLabel::driveDisplayLabel(L'C', kDriveFixed, L""), L"Local Disk (C:)");
}

TEST(DriveLabel, RemovableDriveWithoutVolumeLabelFallsBackToRemovableDisk)
{
    EXPECT_EQ(DriveLabel::driveDisplayLabel(L'E', kDriveRemovable, L""), L"Removable Disk (E:)");
}

TEST(DriveLabel, CdRomDriveWithoutVolumeLabelFallsBackToDvdDrive)
{
    EXPECT_EQ(DriveLabel::driveDisplayLabel(L'D', kDriveCdRom, L""), L"DVD Drive (D:)");
}

TEST(DriveLabel, NetworkDriveWithVolumeLabelUsesVolumeLabel)
{
    EXPECT_EQ(DriveLabel::driveDisplayLabel(L'Z', kDriveRemote, L"Shared"), L"Shared (Z:)");
}

TEST(DriveLabel, NetworkDriveWithoutVolumeLabelFallsBackToNetworkDrive)
{
    EXPECT_EQ(DriveLabel::driveDisplayLabel(L'Z', kDriveRemote, L""), L"Network Drive (Z:)");
}
