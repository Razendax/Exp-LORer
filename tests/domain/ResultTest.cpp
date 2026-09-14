#include <gtest/gtest.h>

#include "Result.h"

TEST(Result, SuccessHoldsValue)
{
    Result<int> result = Result<int>::success(42);

    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.hasError());
    EXPECT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(result.value(), 42);
}

TEST(Result, FailureHoldsError)
{
    Result<int> result = Result<int>::failure(Error(ErrorCode::NotFound, "missing"));

    ASSERT_TRUE(result.hasError());
    EXPECT_FALSE(result.hasValue());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
    EXPECT_EQ(result.error().message, "missing");
}

TEST(ResultVoid, SuccessHasNoError)
{
    Result<void> result = Result<void>::success();

    EXPECT_TRUE(result.hasValue());
    EXPECT_FALSE(result.hasError());
    EXPECT_TRUE(static_cast<bool>(result));
}

TEST(ResultVoid, FailureHoldsError)
{
    Result<void> result = Result<void>::failure(Error(ErrorCode::IoError, "disk error"));

    EXPECT_TRUE(result.hasError());
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_EQ(result.error().code, ErrorCode::IoError);
}
