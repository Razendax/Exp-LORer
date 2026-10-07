#include "BulkRenameViewModel.h"

#include <utility>

#include "FileOperationsController.h"

BulkRenameViewModel::BulkRenameViewModel(BulkRenameUseCase& useCase, FileOperationsController& fileOperationsController,
                                           std::vector<FileNode> selection, QObject* parent)
    : QObject(parent)
    , m_useCase(useCase)
    , m_fileOperationsController(fileOperationsController)
    , m_selection(std::move(selection))
{
    // Relayed for the dialog's lifetime only (this ViewModel is transient, constructed per dialog
    // show) -- the dialog's modal exec() means nothing else can be mid-operation on
    // m_fileOperationsController while this connection is live, so every failure it reports here is
    // genuinely this dialog's own apply() call.
    connect(&m_fileOperationsController, &FileOperationsController::operationFailed, this, &BulkRenameViewModel::operationFailed);

    recomputePreview();
}

void BulkRenameViewModel::updatePattern(const BulkRenamePattern& pattern)
{
    m_pattern = pattern;
    recomputePreview();
}

void BulkRenameViewModel::apply()
{
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> renames;
    renames.reserve(m_preview.size());
    for (const auto& entry : m_preview)
    {
        if (entry.status == BulkRenameUseCase::EntryStatus::Ok)
        {
            renames.emplace_back(entry.source, entry.proposedPath);
        }
    }

    if (renames.empty())
    {
        return;
    }

    m_fileOperationsController.bulkRename(renames);
}

void BulkRenameViewModel::recomputePreview()
{
    auto result = m_useCase.computePreview(m_selection, m_pattern);
    m_preview.clear();
    if (result.hasValue())
    {
        m_preview = std::move(result).value();
        emit previewError(QString());
    }
    else
    {
        emit previewError(QString::fromStdString(result.error().message));
    }

    emit previewChanged(m_preview);
}
