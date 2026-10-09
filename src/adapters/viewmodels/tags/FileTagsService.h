#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include <QObject>
#include <QThread>

#include "FileNode.h"
#include "FileTagsByPath.h"
#include "Result.h"

class IFileSystemRepository;
class FileTagsWorker;

// Owned long-lived by CompositionRoot (constructed once, never destroyed until app shutdown -- same
// lifetime posture as GitStatusService/AdvancedSearchService). Runs every open tab's Tags-column
// lookups (Architecture.md §14.x) on one dedicated background QThread, off the Qt UI thread --
// SQLiteTagRepository is a QSqlDatabase-backed adapter, and disk/DB I/O must never block the UI.
//
// Uses the same context-guarded/callback API GitStatusService/AdvancedSearchService already
// establish (QPointer<QObject> guard(context)), since TabViewModel instances can be destroyed (a
// tab closed) while a request is still in flight.
class FileTagsService : public QObject
{
    Q_OBJECT

public:
    FileTagsService(IFileSystemRepository& fileSystemRepository, const std::filesystem::path& databasePath, QObject* parent = nullptr);
    ~FileTagsService() override;

    void requestTags(QObject* context, const std::vector<FileNode>& files, std::function<void(Result<TagsByPathMap>)> callback);

private:
    QThread m_workerThread;
    FileTagsWorker* m_worker = nullptr;
};
