#include "JsonFileStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QString>

#include "Logging.h"

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
            // Never log file.fileName() (a path) -- storeName is a safe static string, and
            // parseError.errorString() is a generic Qt parser message with no file content in it.
            Logging::warn(std::string(storeName) + ": failed to parse (" + parseError.errorString().toStdString() + ")");
            return std::nullopt;
        }

        Logging::debug(std::string(storeName) + ": loaded");
        return document.object();
    }

    bool trySave(const std::filesystem::path& path, const QJsonObject& root, const char* storeName)
    {
        std::error_code errorCode;
        std::filesystem::create_directories(path.parent_path(), errorCode);

        QSaveFile file(pathToQString(path));
        if (!file.open(QIODevice::WriteOnly))
        {
            Logging::warn(std::string(storeName) + ": failed to open for writing");
            return false;
        }

        file.write(QJsonDocument(root).toJson());

        if (!file.commit())
        {
            Logging::warn(std::string(storeName) + ": write failed");
            return false;
        }

        Logging::debug(std::string(storeName) + ": saved");
        return true;
    }
}
