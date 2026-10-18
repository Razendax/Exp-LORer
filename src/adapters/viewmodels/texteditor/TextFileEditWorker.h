#pragma once

#include <filesystem>
#include <string>

#include <QObject>

#include "Result.h"

class TextFileEditUseCase;

// Runs TextFileEditUseCase's read/write calls on whatever thread it has been moved to
// (Architecture.md §5/§14.98). Owned and driven by TextFileEditViewModel via
// QMetaObject::invokeMethod(..., Qt::QueuedConnection), the same shape FilePreviewWorker/
// FilePreviewViewModel already established -- Result<std::string>/Result<void> aren't
// Qt-meta-type-registered, so a queued functor call is used instead of signals/slots.
class TextFileEditWorker : public QObject
{
    Q_OBJECT

public:
    explicit TextFileEditWorker(TextFileEditUseCase& useCase, QObject* parent = nullptr);

    Result<std::string> load(const std::filesystem::path& path) const;
    Result<void> save(const std::filesystem::path& path, const std::string& content) const;

private:
    TextFileEditUseCase& m_useCase;
};
