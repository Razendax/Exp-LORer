#include <gtest/gtest.h>

#include "Error.h"

TEST(ErrorToLogString, MapsEachCodeToItsName)
{
    EXPECT_EQ(toLogString(ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_EQ(toLogString(ErrorCode::NotFound), "NotFound");
    EXPECT_EQ(toLogString(ErrorCode::AlreadyExists), "AlreadyExists");
    EXPECT_EQ(toLogString(ErrorCode::IoError), "IoError");
    EXPECT_EQ(toLogString(ErrorCode::Unknown), "Unknown");
    EXPECT_EQ(toLogString(ErrorCode::Cancelled), "Cancelled");
    EXPECT_EQ(toLogString(ErrorCode::InsufficientDiskSpace), "InsufficientDiskSpace");
}
