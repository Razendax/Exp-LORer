#pragma once

#include <filesystem>

#include <QObject>
#include <QString>
#include <QThread>

class TextFileEditUseCase;
class TextFileEditWorker;

// Backs the Preview panel's text editor Edit/Save actions (Architecture.md §14.98). Owns one
// dedicated background QThread + TextFileEditWorker, the same "one always-alive worker, not a
// pool" shape FilePreviewViewModel already established -- only one Edit/Save request is ever
// outstanding at a time (the widget disables Edit/Save while a request is in flight), so no
// generation-counter staleness logic is needed here, unlike FilePreviewViewModel's reactive-to-
// selection-changes posture.
class TextFileEditViewModel : public QObject
{
    Q_OBJECT

public:
    explicit TextFileEditViewModel(TextFileEditUseCase& textFileEditUseCase, QObject* parent = nullptr);
    ~TextFileEditViewModel() override;

public slots:
    void requestLoad(std::filesystem::path path);
    void requestSave(std::filesystem::path path, QString content);

signals:
    // Every signal echoes back the path the request was issued for, since only one
    // TextFileEditWorker handles every request serially and a caller juggling more than one
    // path (e.g. the preview widget switching targets while a prior request is still in flight)
    // otherwise has no way to tell which path a response belongs to.
    void loadReady(std::filesystem::path path, QString content);
    void loadFailed(std::filesystem::path path, QString message);
    void saveSucceeded(std::filesystem::path path);
    void saveFailed(std::filesystem::path path, QString message);

private:
    TextFileEditUseCase& m_textFileEditUseCase;

    QThread m_workerThread;
    TextFileEditWorker* m_worker = nullptr;
};
