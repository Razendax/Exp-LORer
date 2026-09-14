#include <gtest/gtest.h>

#include <QCoreApplication>

// SQLiteTagRepositoryTest exercises QSqlDatabase, whose driver-plugin loading needs a
// QCoreApplication instance to exist (Qt resolves the plugin search path from
// QCoreApplication::applicationDirPath()); without one, addDatabase()/open() crash rather than
// failing gracefully. GTest::gtest_main's default main() never constructs one, so this replaces
// it for the whole explorer_adapters_tests binary.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
