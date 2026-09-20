#include "JsonFileStore.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QString>

namespace
{
    QString pathToQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }
}

namespace JsonFileStore
{
    std::optional<QJsonObject> tryLoad(const std::filesystem::path& path, const char* storeName)
    {
        QFile file(pathToQString(path));
        if (!file.open(QIODevice::ReadOnly))
        {
            return std::nullopt;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning().noquote() << QStringLiteral("%1: failed to parse %2: %3")
                                         .arg(QString::fromLatin1(storeName), file.fileName(), parseError.errorString());
            return std::nullopt;
        }

        return document.object();
    }

    bool trySave(const std::filesystem::path& path, const QJsonObject& root, const char* storeName)
    {
        std::error_code errorCode;
        std::filesystem::create_directories(path.parent_path(), errorCode);

        QSaveFile file(pathToQString(path));
        if (!file.open(QIODevice::WriteOnly))
        {
            qWarning().noquote()
                << QStringLiteral("%1: failed to open %2 for writing").arg(QString::fromLatin1(storeName), file.fileName());
            return false;
        }

        file.write(QJsonDocument(root).toJson());

        if (!file.commit())
        {
            qWarning().noquote() << QStringLiteral("%1: failed to save %2").arg(QString::fromLatin1(storeName), file.fileName());
            return false;
        }

        return true;
    }
}
