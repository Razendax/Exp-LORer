#pragma once

#include <filesystem>

#include <QObject>
#include <QString>

#include "NavigationHistory.h"

class FileNavigationUseCase;

// Binds address-bar/navigation-toolbar UI to FileNavigationUseCase and NavigationHistory. A
// precursor to the PaneViewModel described in Architecture.md §14.3 — once multi-tab/split-pane
// support lands, this behavior is absorbed into PaneViewModel rather than kept standalone.
class NavigationViewModel : public QObject
{
    Q_OBJECT

public:
    explicit NavigationViewModel(FileNavigationUseCase& fileNavigationUseCase, QObject* parent = nullptr);

    std::filesystem::path currentPath() const;

public slots:
    // Validates the path via FileNavigationUseCase before recording it in history ("safe
    // navigation"). On failure, history and the current path are left unchanged and
    // navigationFailed is emitted instead.
    void navigateTo(const std::filesystem::path& path);

    // Navigates to the parent of the current path, going through the same validation as
    // navigateTo(). No-op if there is no current path or it is already a filesystem root.
    void goUp();

    // Move within existing history without recording a new entry. Entries were already validated
    // when originally navigated to, so these do not re-invoke FileNavigationUseCase.
    void goBack();
    void goForward();

signals:
    void currentPathChanged(const std::filesystem::path& path);
    void backAvailableChanged(bool available);
    void forwardAvailableChanged(bool available);
    void upAvailableChanged(bool available);
    void navigationFailed(const std::filesystem::path& path, const QString& message);

private:
    void setCurrentPath(std::filesystem::path path);
    void emitAvailability();

    FileNavigationUseCase& m_fileNavigationUseCase;
    NavigationHistory m_history;
};
