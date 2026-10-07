#include <gtest/gtest.h>

#ifdef _WIN32
#include <algorithm>

#include <Windows.h>

#include <QString>

#include "EnvironmentVariableApplier.h"

namespace
{
    QString readEnvironmentVariable(const QString& name)
    {
        const std::wstring wideName = name.toStdWString();
        wchar_t buffer[256] = {};
        const DWORD length = GetEnvironmentVariableW(wideName.c_str(), buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
        if (length == 0)
        {
            return QString();
        }
        return QString::fromWCharArray(buffer, static_cast<int>(length));
    }
}

TEST(EnvironmentVariableApplierTest, ApplySetsEachVariableJoinedWithSemicolons)
{
    EnvironmentVariable variable;
    variable.name = QStringLiteral("EXP_LORER_TEST_VAR_1");
    variable.values = { QStringLiteral("a"), QStringLiteral("b") };

    EnvironmentVariableApplier::apply({ variable }, {});

    EXPECT_EQ(readEnvironmentVariable(variable.name), QStringLiteral("a;b"));

    SetEnvironmentVariableW(variable.name.toStdWString().c_str(), nullptr);
}

TEST(EnvironmentVariableApplierTest, ApplyClearsAPreviouslyAppliedVariableNoLongerPresent)
{
    EnvironmentVariable variable;
    variable.name = QStringLiteral("EXP_LORER_TEST_VAR_2");
    variable.values = { QStringLiteral("x") };

    EnvironmentVariableApplier::apply({ variable }, {});
    ASSERT_EQ(readEnvironmentVariable(variable.name), QStringLiteral("x"));

    EnvironmentVariableApplier::apply({}, { variable.name });

    EXPECT_TRUE(readEnvironmentVariable(variable.name).isEmpty());
}

TEST(EnvironmentVariableApplierTest, CurrentProcessEnvironmentSnapshotIncludesAVariableSplitOnSemicolons)
{
    SetEnvironmentVariableW(L"EXP_LORER_TEST_SNAPSHOT_VAR", L"alpha;beta");

    const std::vector<EnvironmentVariable> snapshot = EnvironmentVariableApplier::currentProcessEnvironmentSnapshot();

    const auto found = std::find_if(snapshot.begin(), snapshot.end(), [](const EnvironmentVariable& variable) {
        return variable.name.compare(QStringLiteral("EXP_LORER_TEST_SNAPSHOT_VAR"), Qt::CaseInsensitive) == 0;
    });
    ASSERT_NE(found, snapshot.end());
    ASSERT_EQ(found->values.size(), 2);
    EXPECT_EQ(found->values[0], QStringLiteral("alpha"));
    EXPECT_EQ(found->values[1], QStringLiteral("beta"));

    SetEnvironmentVariableW(L"EXP_LORER_TEST_SNAPSHOT_VAR", nullptr);
}

TEST(EnvironmentVariableApplierTest, CurrentProcessEnvironmentSnapshotSkipsHiddenPerDrivePseudoVariables)
{
    const std::vector<EnvironmentVariable> snapshot = EnvironmentVariableApplier::currentProcessEnvironmentSnapshot();

    for (const EnvironmentVariable& variable : snapshot)
    {
        EXPECT_FALSE(variable.name.startsWith(QLatin1Char('=')));
    }
}
#endif
