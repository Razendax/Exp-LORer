#include "BulkRenameDialog.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "BulkRenamePattern.h"
#include "BulkRenameViewModel.h"
#include "UiColors.h"

namespace
{
    constexpr int kOriginalNameColumn = 0;
    constexpr int kNewNameColumn = 1;

    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    // One tooltip/reason per non-Ok, non-Unchanged status -- empty for a row the "Rename" button
    // doesn't need to block on.
    QString blockingReason(BulkRenameUseCase::EntryStatus status)
    {
        switch (status)
        {
            case BulkRenameUseCase::EntryStatus::InvalidName:
                return BulkRenameDialog::tr("Proposed name contains a character that isn't allowed (\\ / : * ? \" < > |).");
            case BulkRenameUseCase::EntryStatus::CollisionWithinBatch:
                return BulkRenameDialog::tr("Proposed name collides with another renamed item in this batch.");
            case BulkRenameUseCase::EntryStatus::CollisionWithExisting:
                return BulkRenameDialog::tr("An item with this name already exists here.");
            case BulkRenameUseCase::EntryStatus::Ok:
            case BulkRenameUseCase::EntryStatus::Unchanged:
                return QString();
        }
        return QString();
    }

    bool blocksRename(BulkRenameUseCase::EntryStatus status)
    {
        return status == BulkRenameUseCase::EntryStatus::InvalidName ||
               status == BulkRenameUseCase::EntryStatus::CollisionWithinBatch ||
               status == BulkRenameUseCase::EntryStatus::CollisionWithExisting;
    }
}

BulkRenameDialog::BulkRenameDialog(BulkRenameViewModel* viewModel, QWidget* parent)
    : QDialog(parent)
    , m_viewModel(viewModel)
{
    setObjectName(QStringLiteral("bulkRenameDialog"));
    setWindowTitle(tr("Bulk Rename"));
    resize(720, 560);

    auto* mainLayout = new QVBoxLayout(this);

    auto* findReplaceGroup = new QGroupBox(tr("Find && Replace"), this);
    auto* findReplaceLayout = new QFormLayout(findReplaceGroup);
    m_findEdit = new QLineEdit(findReplaceGroup);
    m_replaceEdit = new QLineEdit(findReplaceGroup);
    m_regexCheckBox = new QCheckBox(tr("Regex"), findReplaceGroup);
    m_matchCaseCheckBox = new QCheckBox(tr("Match case"), findReplaceGroup);
    findReplaceLayout->addRow(tr("Find:"), m_findEdit);
    findReplaceLayout->addRow(tr("Replace:"), m_replaceEdit);
    auto* findReplaceOptionsLayout = new QHBoxLayout();
    findReplaceOptionsLayout->addWidget(m_regexCheckBox);
    findReplaceOptionsLayout->addWidget(m_matchCaseCheckBox);
    findReplaceOptionsLayout->addStretch(1);
    findReplaceLayout->addRow(QString(), findReplaceOptionsLayout);
    m_patternErrorLabel = new QLabel(findReplaceGroup);
    m_patternErrorLabel->setWordWrap(true);
    m_patternErrorLabel->setStyleSheet(QStringLiteral("color: %1;").arg(QLatin1String(UiColors::current().notificationErrorRed)));
    m_patternErrorLabel->hide();
    findReplaceLayout->addRow(QString(), m_patternErrorLabel);
    mainLayout->addWidget(findReplaceGroup);

    auto* caseGroup = new QGroupBox(tr("Case"), this);
    auto* caseLayout = new QFormLayout(caseGroup);
    m_caseTransformCombo = new QComboBox(caseGroup);
    m_caseTransformCombo->addItem(tr("No change"));
    m_caseTransformCombo->addItem(tr("UPPERCASE"));
    m_caseTransformCombo->addItem(tr("lowercase"));
    m_caseTransformCombo->addItem(tr("Title Case"));
    caseLayout->addRow(tr("Transform:"), m_caseTransformCombo);
    mainLayout->addWidget(caseGroup);

    auto* counterGroup = new QGroupBox(tr("Counter"), this);
    auto* counterLayout = new QFormLayout(counterGroup);
    m_counterEnabledCheckBox = new QCheckBox(tr("Add a sequential counter"), counterGroup);
    counterLayout->addRow(m_counterEnabledCheckBox);
    m_counterStartSpinBox = new QSpinBox(counterGroup);
    m_counterStartSpinBox->setRange(-999999, 999999);
    m_counterStartSpinBox->setValue(1);
    counterLayout->addRow(tr("Start:"), m_counterStartSpinBox);
    m_counterStepSpinBox = new QSpinBox(counterGroup);
    m_counterStepSpinBox->setRange(-999999, 999999);
    m_counterStepSpinBox->setValue(1);
    counterLayout->addRow(tr("Step:"), m_counterStepSpinBox);
    m_counterDigitsSpinBox = new QSpinBox(counterGroup);
    m_counterDigitsSpinBox->setRange(1, 10);
    m_counterDigitsSpinBox->setValue(1);
    counterLayout->addRow(tr("Digits:"), m_counterDigitsSpinBox);
    m_counterPositionCombo = new QComboBox(counterGroup);
    m_counterPositionCombo->addItem(tr("Prefix"));
    m_counterPositionCombo->addItem(tr("Suffix"));
    m_counterPositionCombo->setCurrentIndex(1);
    counterLayout->addRow(tr("Position:"), m_counterPositionCombo);
    m_counterSeparatorEdit = new QLineEdit(counterGroup);
    m_counterSeparatorEdit->setText(QStringLiteral("_"));
    counterLayout->addRow(tr("Separator:"), m_counterSeparatorEdit);
    mainLayout->addWidget(counterGroup);

    auto* extensionGroup = new QGroupBox(tr("Extension"), this);
    auto* extensionLayout = new QFormLayout(extensionGroup);
    m_extensionOverrideCheckBox = new QCheckBox(tr("Override extension"), extensionGroup);
    extensionLayout->addRow(m_extensionOverrideCheckBox);
    m_extensionEdit = new QLineEdit(extensionGroup);
    m_extensionEdit->setEnabled(false);
    extensionLayout->addRow(tr("New extension:"), m_extensionEdit);
    mainLayout->addWidget(extensionGroup);

    m_previewTable = new QTableWidget(0, 2, this);
    m_previewTable->setObjectName(QStringLiteral("bulkRenamePreviewTable"));
    m_previewTable->setHorizontalHeaderLabels({ tr("Original Name"), tr("New Name") });
    m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_previewTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_previewTable->horizontalHeader()->setStretchLastSection(true);
    m_previewTable->horizontalHeader()->setSectionResizeMode(kOriginalNameColumn, QHeaderView::Interactive);
    m_previewTable->horizontalHeader()->resizeSection(kOriginalNameColumn, 320);
    m_previewTable->verticalHeader()->setVisible(false);
    mainLayout->addWidget(m_previewTable, 1);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    m_renameButton = new QPushButton(tr("Rename"), this);
    m_renameButton->setObjectName(QStringLiteral("bulkRenameRenameButton"));
    m_renameButton->setEnabled(false);
    auto* cancelButton = new QPushButton(tr("Cancel"), this);
    buttonLayout->addWidget(m_renameButton);
    buttonLayout->addWidget(cancelButton);
    mainLayout->addLayout(buttonLayout);

    connect(m_findEdit, &QLineEdit::textChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_replaceEdit, &QLineEdit::textChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_regexCheckBox, &QCheckBox::toggled, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_matchCaseCheckBox, &QCheckBox::toggled, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_caseTransformCombo, &QComboBox::currentIndexChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_counterEnabledCheckBox, &QCheckBox::toggled, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_counterStartSpinBox, &QSpinBox::valueChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_counterStepSpinBox, &QSpinBox::valueChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_counterDigitsSpinBox, &QSpinBox::valueChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_counterPositionCombo, &QComboBox::currentIndexChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_counterSeparatorEdit, &QLineEdit::textChanged, this, &BulkRenameDialog::onFieldsChanged);
    connect(m_extensionOverrideCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_extensionEdit->setEnabled(checked);
        onFieldsChanged();
    });
    connect(m_extensionEdit, &QLineEdit::textChanged, this, &BulkRenameDialog::onFieldsChanged);

    connect(m_renameButton, &QPushButton::clicked, this, &BulkRenameDialog::onRenameClicked);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_viewModel, &BulkRenameViewModel::previewChanged, this, &BulkRenameDialog::rebuildPreviewTable);
    connect(m_viewModel, &BulkRenameViewModel::previewError, this, &BulkRenameDialog::onPreviewError);
    connect(m_viewModel, &BulkRenameViewModel::operationFailed, this, &BulkRenameDialog::onOperationFailed);

    rebuildPreviewTable(m_viewModel->preview());
}

void BulkRenameDialog::rebuildPreviewTable(const std::vector<BulkRenameUseCase::PreviewEntry>& entries)
{
    // Theme-aware text color rather than a fixed background (Architecture.md §14.50 convention,
    // same as MonitoringLogModel::data's ForegroundRole use) -- a hardcoded light background
    // combined with the dark theme's light default text would otherwise be unreadable.
    const QColor warningForeground(QLatin1String(UiColors::current().notificationErrorRed));

    m_previewTable->setRowCount(static_cast<int>(entries.size()));
    m_hasBlockingRow = false;

    for (int row = 0; row < static_cast<int>(entries.size()); ++row)
    {
        const BulkRenameUseCase::PreviewEntry& entry = entries[static_cast<std::size_t>(row)];

        auto* originalItem = new QTableWidgetItem(toQString(entry.source.filename()));
        auto* newNameItem = new QTableWidgetItem(toQString(entry.proposedPath.filename()));

        const QString reason = blockingReason(entry.status);
        if (!reason.isEmpty())
        {
            originalItem->setForeground(warningForeground);
            newNameItem->setForeground(warningForeground);
            originalItem->setToolTip(reason);
            newNameItem->setToolTip(reason);
        }
        else if (entry.status == BulkRenameUseCase::EntryStatus::Unchanged)
        {
            newNameItem->setToolTip(tr("Name is unchanged -- this item will be skipped."));
        }

        if (blocksRename(entry.status))
        {
            m_hasBlockingRow = true;
        }

        m_previewTable->setItem(row, kOriginalNameColumn, originalItem);
        m_previewTable->setItem(row, kNewNameColumn, newNameItem);
    }

    m_renameButton->setEnabled(!m_hasBlockingRow && !entries.empty());
}

void BulkRenameDialog::onFieldsChanged()
{
    BulkRenamePattern pattern;
    pattern.findText = m_findEdit->text().toStdString();
    pattern.replaceText = m_replaceEdit->text().toStdString();
    pattern.useRegex = m_regexCheckBox->isChecked();
    pattern.matchCase = m_matchCaseCheckBox->isChecked();
    pattern.caseTransform = static_cast<BulkRenamePattern::CaseTransform>(m_caseTransformCombo->currentIndex());

    pattern.counterEnabled = m_counterEnabledCheckBox->isChecked();
    pattern.counterStart = m_counterStartSpinBox->value();
    pattern.counterStep = m_counterStepSpinBox->value();
    pattern.counterDigits = m_counterDigitsSpinBox->value();
    pattern.counterPosition = static_cast<BulkRenamePattern::CounterPosition>(m_counterPositionCombo->currentIndex());
    pattern.counterSeparator = m_counterSeparatorEdit->text().toStdString();

    if (m_extensionOverrideCheckBox->isChecked())
    {
        pattern.newExtension = m_extensionEdit->text().toStdString();
    }

    m_viewModel->updatePattern(pattern);
}

void BulkRenameDialog::onRenameClicked()
{
    // apply() is a direct (same-thread, non-queued) call: a synchronous operationFailed during it
    // sets `failed` before apply() returns, so we know whether to stay open before deciding.
    bool failed = false;
    const QMetaObject::Connection connection =
        connect(m_viewModel, &BulkRenameViewModel::operationFailed, this, [&failed](const QString&) { failed = true; });

    m_viewModel->apply();

    disconnect(connection);
    if (!failed)
    {
        accept();
    }
}

void BulkRenameDialog::onOperationFailed(const QString& message)
{
    QMessageBox::warning(this, tr("Bulk Rename"), message);
}

void BulkRenameDialog::onPreviewError(const QString& message)
{
    // Shown inline rather than as a modal QMessageBox: this fires on every keystroke while the
    // pattern is live-edited (e.g. a regex that's merely incomplete mid-edit, like "\d" after only
    // "\"), so a popup here would interrupt typing on practically every character.
    m_patternErrorLabel->setText(message);
    m_patternErrorLabel->setVisible(!message.isEmpty());
}
