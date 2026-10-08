#include "DuplicateFinderDialog.h"

#include <utility>

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTreeView>
#include <QVBoxLayout>

#include "DuplicateFinderService.h"
#include "DuplicateGroupsModel.h"
#include "FileNavigationUseCase.h"
#include "FileSizeFormat.h"
#include "NotificationCenter.h"

DuplicateFinderDialog::DuplicateFinderDialog(DuplicateFinderService& duplicateFinderService,
                                              FileNavigationUseCase& fileNavigationUseCase,
                                              NotificationCenter& notificationCenter, const std::filesystem::path& initialPath,
                                              QWidget* parent)
    : QDialog(parent)
    , m_duplicateFinderService(duplicateFinderService)
    , m_fileNavigationUseCase(fileNavigationUseCase)
    , m_notificationCenter(notificationCenter)
{
    setWindowTitle(tr("Find Duplicate Files"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(800, 600);

    auto* mainLayout = new QVBoxLayout(this);

    auto* rootRow = new QHBoxLayout();
    m_rootPathEdit = new QLineEdit(this);
    m_rootPathEdit->setReadOnly(true);
    m_rootPathEdit->setText(QString::fromStdWString(initialPath.wstring()));
    rootRow->addWidget(m_rootPathEdit, 1);

    m_browseButton = new QPushButton(tr("Browse..."), this);
    rootRow->addWidget(m_browseButton);
    mainLayout->addLayout(rootRow);

    auto* sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(tr("Minimum file size:"), this));

    m_minSizeSpinBox = new QSpinBox(this);
    m_minSizeSpinBox->setRange(0, 999999);
    m_minSizeSpinBox->setValue(4);
    sizeRow->addWidget(m_minSizeSpinBox);

    m_minSizeUnitCombo = new QComboBox(this);
    m_minSizeUnitCombo->addItems({ tr("B"), tr("KB"), tr("MB") });
    m_minSizeUnitCombo->setCurrentIndex(1);
    sizeRow->addWidget(m_minSizeUnitCombo);
    sizeRow->addStretch(1);
    mainLayout->addLayout(sizeRow);

    auto* scanButtonRow = new QHBoxLayout();
    m_scanFolderButton = new QPushButton(tr("Scan Folder"), this);
    scanButtonRow->addWidget(m_scanFolderButton);
    m_scanDriveButton = new QPushButton(tr("Scan Drive"), this);
    scanButtonRow->addWidget(m_scanDriveButton);
    scanButtonRow->addStretch(1);
    mainLayout->addLayout(scanButtonRow);

    m_resultsModel = new DuplicateGroupsModel(this);
    m_resultsView = new QTreeView(this);
    m_resultsView->setObjectName(QStringLiteral("duplicateFinderResultsView"));
    m_resultsView->setModel(m_resultsModel);
    m_resultsView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultsView->setUniformRowHeights(true);
    // Plain Interactive columns on every section, same as FileBrowserView -- Stretch on a tree
    // column fights QTreeView's row indentation (the header cell isn't indented but the content
    // area is), which made the Name column's boundary visibly disagree with its header while
    // dragging. Fixed starting widths stand in for FileBrowserView's persisted columnWidths()
    // since duplicate-scan results aren't saved between sessions.
    m_resultsView->header()->setStretchLastSection(true);
    m_resultsView->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_resultsView->header()->resizeSection(DuplicateGroupsModel::NameColumn, 320);
    m_resultsView->header()->resizeSection(DuplicateGroupsModel::FolderColumn, 320);
    mainLayout->addWidget(m_resultsView, 1);

    auto* helperButtonRow = new QHBoxLayout();
    m_checkAllButNewestButton = new QPushButton(tr("Check All but Newest in Each Group"), this);
    helperButtonRow->addWidget(m_checkAllButNewestButton);
    m_checkAllButOldestButton = new QPushButton(tr("Check All but Oldest in Each Group"), this);
    helperButtonRow->addWidget(m_checkAllButOldestButton);
    helperButtonRow->addStretch(1);
    m_deleteCheckedButton = new QPushButton(tr("Delete Checked"), this);
    helperButtonRow->addWidget(m_deleteCheckedButton);
    mainLayout->addLayout(helperButtonRow);

    connect(m_browseButton, &QPushButton::clicked, this, &DuplicateFinderDialog::browseForFolder);
    connect(m_scanFolderButton, &QPushButton::clicked, this, &DuplicateFinderDialog::scanFolder);
    connect(m_scanDriveButton, &QPushButton::clicked, this, &DuplicateFinderDialog::scanDrive);
    connect(m_checkAllButNewestButton, &QPushButton::clicked, m_resultsModel, &DuplicateGroupsModel::checkAllButNewestInEachGroup);
    connect(m_checkAllButOldestButton, &QPushButton::clicked, m_resultsModel, &DuplicateGroupsModel::checkAllButOldestInEachGroup);
    connect(m_deleteCheckedButton, &QPushButton::clicked, this, &DuplicateFinderDialog::deleteChecked);
    connect(m_resultsView, &QTreeView::customContextMenuRequested, this, &DuplicateFinderDialog::showResultsContextMenu);
    // A group row's double-click keeps QTreeView's default expand/collapse; only file rows open.
    connect(m_resultsView, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (!m_resultsModel->isFileRow(index))
        {
            return;
        }
        auto result = m_fileNavigationUseCase.openFile(m_resultsModel->pathForIndex(index));
        if (result.hasError())
        {
            m_notificationCenter.showPopup(NotificationType::Error, tr("Could Not Open File"),
                                            QString::fromStdString(result.error().message));
        }
    });

    // Only this dialog's own in-flight scan (if any) cares about a Cancel click -- every other
    // progress card's cancel request is somebody else's concern, same filter-by-id posture
    // FileOperationsController uses implicitly by owning its own ids.
    connect(&m_notificationCenter, &NotificationCenter::progressCancelRequested, this, [this](NotificationCenter::ProgressId id) {
        if (m_scanning && id == m_activeRequestId)
        {
            cancelActiveScan();
        }
    });

    updateButtonsEnabled();
}

DuplicateFinderDialog::~DuplicateFinderDialog() = default;

void DuplicateFinderDialog::closeEvent(QCloseEvent* event)
{
    if (m_scanning)
    {
        cancelActiveScan();
    }
    QDialog::closeEvent(event);
}

void DuplicateFinderDialog::browseForFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Folder"), m_rootPathEdit->text());
    if (!dir.isEmpty())
    {
        m_rootPathEdit->setText(dir);
    }
}

void DuplicateFinderDialog::scanFolder()
{
    const std::filesystem::path root(m_rootPathEdit->text().toStdWString());
    if (root.empty())
    {
        return;
    }
    startScan(root);
}

void DuplicateFinderDialog::scanDrive()
{
    const std::filesystem::path root(m_rootPathEdit->text().toStdWString());
    if (root.empty())
    {
        return;
    }
    const std::filesystem::path driveRoot = root.root_path();
    if (driveRoot.empty())
    {
        return;
    }
    startScan(driveRoot);
}

void DuplicateFinderDialog::startScan(const std::filesystem::path& root)
{
    if (m_scanning)
    {
        return;
    }

    // Starting a new scan clears any previous results immediately.
    m_resultsModel->clear();

    m_scanning = true;
    updateButtonsEnabled();

    m_activeRequestId = m_notificationCenter.createProgressNotification(tr("Scanning for Duplicates"), /*barCount=*/1,
                                                                          /*cancellable=*/true);

    DuplicateFinderService::Callbacks callbacks;
    callbacks.onProgress = [this](std::size_t hashed, std::size_t totalCandidates) { onScanProgress(hashed, totalCandidates); };
    callbacks.onFinished = [this](Result<std::vector<DuplicateGroupInfo>> result) { onScanFinished(std::move(result)); };

    m_duplicateFinderService.requestScan(this, m_activeRequestId, root, minFileSizeBytes(), std::move(callbacks));
}

void DuplicateFinderDialog::cancelActiveScan()
{
    m_duplicateFinderService.cancel(m_activeRequestId);
}

void DuplicateFinderDialog::onScanProgress(std::size_t hashed, std::size_t totalCandidates)
{
    const int max = totalCandidates > 0 ? static_cast<int>(totalCandidates) : 0;
    const int current = static_cast<int>(hashed);
    m_notificationCenter.updateProgressBar(m_activeRequestId, 0, current, max, tr("Scanning for duplicates"),
                                            tr("%1 / %2 files hashed").arg(hashed).arg(totalCandidates), QString());
}

void DuplicateFinderDialog::onScanFinished(Result<std::vector<DuplicateGroupInfo>> result)
{
    m_notificationCenter.terminateProgressNotification(m_activeRequestId);
    m_scanning = false;
    updateButtonsEnabled();

    if (result.hasError())
    {
        if (result.error().code != ErrorCode::Cancelled)
        {
            m_notificationCenter.showPopup(NotificationType::Error, tr("Duplicate Scan Failed"),
                                            QString::fromStdString(result.error().message));
        }
        return;
    }

    m_resultsModel->setGroups(std::move(result).value());
}

void DuplicateFinderDialog::deleteChecked()
{
    const auto checkedPaths = m_resultsModel->checkedPaths();
    if (checkedPaths.empty())
    {
        return;
    }

    const std::uintmax_t checkedBytes = m_resultsModel->checkedBytes();
    const auto answer = QMessageBox::question(
        this, tr("Delete Checked Files"),
        tr("Move %1 file(s) (%2) to the Recycle Bin?").arg(checkedPaths.size()).arg(FileSizeFormat::format(checkedBytes)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        return;
    }

    std::vector<std::filesystem::path> deleted;
    QStringList failures;
    for (const auto& path : checkedPaths)
    {
        auto deleteResult = m_fileNavigationUseCase.moveFileToTrash(path);
        if (deleteResult.hasValue())
        {
            deleted.push_back(path);
        }
        else
        {
            failures.append(QString::fromStdWString(path.filename().wstring()));
        }
    }

    // Deleted rows disappear from the tree live; a group that drops to a single remaining member
    // collapses out of the results entirely.
    m_resultsModel->removePaths(deleted);

    if (!failures.isEmpty())
    {
        m_notificationCenter.showPopup(NotificationType::Error, tr("Some Files Could Not Be Deleted"),
                                        tr("Failed to delete: %1").arg(failures.join(QStringLiteral(", "))));
    }
}

void DuplicateFinderDialog::showResultsContextMenu(const QPoint& pos)
{
    const QModelIndex index = m_resultsView->indexAt(pos);
    if (!m_resultsModel->isFileRow(index))
    {
        return;
    }

    const std::filesystem::path path = m_resultsModel->pathForIndex(index);

    QMenu menu(this);
    menu.addAction(tr("Open Containing Folder"), this, [this, path]() { emit openContainingFolderRequested(path); });
    menu.exec(m_resultsView->viewport()->mapToGlobal(pos));
}

void DuplicateFinderDialog::updateButtonsEnabled()
{
    m_scanFolderButton->setEnabled(!m_scanning);
    m_scanDriveButton->setEnabled(!m_scanning);
    m_scanFolderButton->setText(m_scanning ? tr("Scanning...") : tr("Scan Folder"));
    m_scanDriveButton->setText(m_scanning ? tr("Scanning...") : tr("Scan Drive"));
    m_browseButton->setEnabled(!m_scanning);
}

std::uintmax_t DuplicateFinderDialog::minFileSizeBytes() const
{
    std::uintmax_t multiplier = 1;
    switch (m_minSizeUnitCombo->currentIndex())
    {
        case 0: // B
            multiplier = 1;
            break;
        case 1: // KB
            multiplier = 1024;
            break;
        case 2: // MB
            multiplier = 1024 * 1024;
            break;
        default:
            break;
    }
    return static_cast<std::uintmax_t>(m_minSizeSpinBox->value()) * multiplier;
}
