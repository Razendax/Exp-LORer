#pragma once

#include <vector>

#include <QObject>
#include <QString>

#include "BulkRenamePattern.h"
#include "BulkRenameUseCase.h"
#include "FileNode.h"

class FileOperationsController;

// Backs the Bulk Rename dialog (Architecture.md §14.55): a thin ViewModel over BulkRenameUseCase,
// same "thin-dialog-over-a-ViewModel" shape as TagManagerViewModel/ManageFavoritesDialog's
// FavoritesViewModel. Constructed transiently per dialog show, not owned long-lived by
// CompositionRoot -- same posture as TagManagerViewModel in MainWindow::showTagManagerDialog.
// Untested per the codebase's existing UI/ViewModel convention (Architecture.md §11).
class BulkRenameViewModel : public QObject
{
    Q_OBJECT

public:
    BulkRenameViewModel(BulkRenameUseCase& useCase, FileOperationsController& fileOperationsController,
                         std::vector<FileNode> selection, QObject* parent = nullptr);

    const std::vector<BulkRenameUseCase::PreviewEntry>& preview() const noexcept { return m_preview; }

public slots:
    void updatePattern(const BulkRenamePattern& pattern);

    // Filters the current preview to EntryStatus::Ok entries and calls
    // FileOperationsController::bulkRename with them.
    void apply();

signals:
    void previewChanged(const std::vector<BulkRenameUseCase::PreviewEntry>& entries);

    // Fires on every recomputePreview() (live, as the user types) with the current pattern's
    // validation message, or an empty string once the pattern is valid again -- kept distinct from
    // operationFailed so the dialog can show it inline instead of a modal popup interrupting typing
    // (e.g. a regex that's merely incomplete mid-edit, like "\d" after only "\").
    void previewError(const QString& message);

    // Fires only for a real apply()-time failure relayed from FileOperationsController -- a modal
    // popup is appropriate here since it reports the outcome of a one-shot user action, not a
    // live-typing validation state.
    void operationFailed(const QString& message);

private:
    void recomputePreview();

    BulkRenameUseCase& m_useCase;
    FileOperationsController& m_fileOperationsController;
    std::vector<FileNode> m_selection;
    BulkRenamePattern m_pattern;
    std::vector<BulkRenameUseCase::PreviewEntry> m_preview;
};
