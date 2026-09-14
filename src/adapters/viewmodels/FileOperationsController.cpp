#include "FileOperationsController.h"

#include <algorithm>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QUrl>

#include "FileNavigationUseCase.h"

namespace
{
    constexpr quint32 kDropEffectCopy = 1;
    constexpr quint32 kDropEffectMove = 2;
    constexpr char kPreferredDropEffectFormat[] = "Preferred DropEffect";

    QByteArray dropEffectBytes(quint32 effect)
    {
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << effect;
        return bytes;
    }

    quint32 readDropEffect(const QMimeData& mimeData)
    {
        const QString format = QString::fromLatin1(kPreferredDropEffectFormat);
        if (!mimeData.hasFormat(format))
        {
            return 0;
        }

        QByteArray bytes = mimeData.data(format);
        QDataStream stream(&bytes, QIODevice::ReadOnly);
        stream.setByteOrder(QDataStream::LittleEndian);

        quint32 effect = 0;
        stream >> effect;
        return effect;
    }

    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }
}

FileOperationsController::FileOperationsController(FileNavigationUseCase& fileNavigationUseCase, QObject* parent)
    : QObject(parent)
    , m_fileNavigationUseCase(fileNavigationUseCase)
{
}

void FileOperationsController::copyToClipboard(const std::filesystem::path& source)
{
    auto* mimeData = new QMimeData();
    mimeData->setUrls({QUrl::fromLocalFile(toQString(source))});
    mimeData->setData(QString::fromLatin1(kPreferredDropEffectFormat), dropEffectBytes(kDropEffectCopy));
    QApplication::clipboard()->setMimeData(mimeData);
}

void FileOperationsController::cutToClipboard(const std::filesystem::path& source)
{
    auto* mimeData = new QMimeData();
    mimeData->setUrls({QUrl::fromLocalFile(toQString(source))});
    mimeData->setData(QString::fromLatin1(kPreferredDropEffectFormat), dropEffectBytes(kDropEffectMove));
    QApplication::clipboard()->setMimeData(mimeData);
}

void FileOperationsController::pasteInto(const std::filesystem::path& destinationDirectory)
{
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasUrls())
    {
        return;
    }

    const bool isMove = (readDropEffect(*mimeData) & kDropEffectMove) != 0;

    QStringList failures;
    std::vector<std::filesystem::path> sourceParents;

    for (const QUrl& url : mimeData->urls())
    {
        if (!url.isLocalFile())
        {
            continue;
        }

        const std::filesystem::path source(url.toLocalFile().toStdWString());
        const std::filesystem::path candidate = uniqueDestinationName(destinationDirectory, source.filename());

        auto result = isMove ? m_fileNavigationUseCase.moveFile(source, candidate)
                              : m_fileNavigationUseCase.copyFile(source, candidate);

        if (result.hasError())
        {
            failures.append(QString::fromStdString(result.error().message));
            continue;
        }

        if (isMove)
        {
            sourceParents.push_back(source.parent_path());
        }
    }

    emit directoryContentsMayHaveChanged(destinationDirectory);

    std::sort(sourceParents.begin(), sourceParents.end());
    sourceParents.erase(std::unique(sourceParents.begin(), sourceParents.end()), sourceParents.end());
    for (const auto& parent : sourceParents)
    {
        emit directoryContentsMayHaveChanged(parent);
    }

    if (isMove)
    {
        QApplication::clipboard()->clear();
    }

    if (!failures.isEmpty())
    {
        emit operationFailed(failures.join(QStringLiteral("; ")));
    }
}

void FileOperationsController::moveToTrash(const std::filesystem::path& path)
{
    auto result = m_fileNavigationUseCase.moveFileToTrash(path);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    emit directoryContentsMayHaveChanged(path.parent_path());
}

void FileOperationsController::deletePermanently(const std::filesystem::path& path)
{
    auto result = m_fileNavigationUseCase.deleteFilePermanently(path);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
        return;
    }

    emit directoryContentsMayHaveChanged(path.parent_path());
}

void FileOperationsController::openFile(const std::filesystem::path& path)
{
    auto result = m_fileNavigationUseCase.openFile(path);
    if (result.hasError())
    {
        emit operationFailed(QString::fromStdString(result.error().message));
    }
}

std::filesystem::path FileOperationsController::uniqueDestinationName(const std::filesystem::path& destinationDirectory,
                                                                       const std::filesystem::path& desiredName) const
{
    const std::filesystem::path candidate = destinationDirectory / desiredName;
    if (m_fileNavigationUseCase.stat(candidate).hasError())
    {
        return candidate;
    }

    const std::filesystem::path stem = desiredName.stem();
    const std::filesystem::path extension = desiredName.extension();

    for (int counter = 2;; ++counter)
    {
        std::filesystem::path attemptName = stem;
        attemptName += L" (" + std::to_wstring(counter) + L")";
        attemptName += extension;

        const std::filesystem::path attempt = destinationDirectory / attemptName;
        if (m_fileNavigationUseCase.stat(attempt).hasError())
        {
            return attempt;
        }
    }
}
