#pragma once

#include <vector>

#include <QDialog>

#include "BulkRenameUseCase.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class BulkRenameViewModel;

// Modal dialog for the Bulk Rename tool (Architecture.md §14.55): pattern controls above a live
// before->after preview table, rebuilt on every BulkRenameViewModel::previewChanged -- same
// "rebuild whole view on signal" posture as TagManagerDialog::rebuildTagChips/ManageFavoritesDialog.
// Constructed with a BulkRenameViewModel*, same constructor shape as TagManagerDialog.
class BulkRenameDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BulkRenameDialog(BulkRenameViewModel* viewModel, QWidget* parent = nullptr);

private:
    void rebuildPreviewTable(const std::vector<BulkRenameUseCase::PreviewEntry>& entries);
    void onFieldsChanged();
    void onRenameClicked();
    void onOperationFailed(const QString& message);
    void onPreviewError(const QString& message);

    BulkRenameViewModel* m_viewModel = nullptr;

    QLineEdit* m_findEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QCheckBox* m_regexCheckBox = nullptr;
    QCheckBox* m_matchCaseCheckBox = nullptr;
    QLabel* m_patternErrorLabel = nullptr;

    QComboBox* m_caseTransformCombo = nullptr;

    QCheckBox* m_counterEnabledCheckBox = nullptr;
    QSpinBox* m_counterStartSpinBox = nullptr;
    QSpinBox* m_counterStepSpinBox = nullptr;
    QSpinBox* m_counterDigitsSpinBox = nullptr;
    QComboBox* m_counterPositionCombo = nullptr;
    QLineEdit* m_counterSeparatorEdit = nullptr;

    QLineEdit* m_extensionEdit = nullptr;
    QCheckBox* m_extensionOverrideCheckBox = nullptr;

    QTableWidget* m_previewTable = nullptr;
    QPushButton* m_renameButton = nullptr;

    bool m_hasBlockingRow = false;
};
