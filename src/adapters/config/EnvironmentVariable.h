#pragma once

#include <QString>
#include <QStringList>

// One per-process environment variable managed by the Environment Variables dialog
// (Architecture.md §14.57): a name plus an ordered list of values, joined with ";" when applied to
// the process environment or shown in the dialog's table. Pure value type, no Qt object -- same
// posture as ColumnVisibilitySettings.
struct EnvironmentVariable
{
    QString name;
    QStringList values;
};

inline QString joinEnvironmentVariableValues(const QStringList& values)
{
    return values.join(QStringLiteral(";"));
}
