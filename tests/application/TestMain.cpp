#include <gmock/gmock.h>
#include <gtest/gtest.h>

// vcpkg's dynamic-triplet gmock.dll statically embeds its own copy of the gtest registry
// instead of linking gtest.dll, so the prebuilt GTest::gtest_main/GTest::gmock_main mains
// end up checking a different registry than the one TEST()/MOCK_METHOD register into here,
// making every test silently fail to be discovered. Providing our own main() sidesteps that
// split-registry issue since registration and RUN_ALL_TESTS() are resolved consistently.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
