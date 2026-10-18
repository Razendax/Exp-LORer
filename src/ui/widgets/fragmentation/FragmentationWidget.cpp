#include "FragmentationWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeView>
#include <QVBoxLayout>

#include "FileSizeFormat.h"
#include "FragmentationResultsModel.h"
#include "FragmentationViewModel.h"
#include "MinFileSizeInput.h"
#include "NotificationCenter.h"
#include "TabViewModel.h"
#include "VirtualPaths.h"

FragmentationWidget::FragmentationWidget(FragmentationService& service, NotificationCenter& notificationCenter, TabViewModel* tab,
                                           QWidget* parent)
    : QWidget(parent)
    , m_tab(tab)
{
    m_viewModel = new FragmentationViewModel(service, notificationCenter, tab, this);

    auto* mainLayout = new QVBoxLayout(this);

    auto* toolbarRow = new QHBoxLayout();
    m_scanFolderButton = new QPushButton(tr("Scan Folder"), this);
    m_scanFolderButton->setObjectName(QStringLiteral("fragmentationScanFolderButton"));
    toolbarRow->addWidget(m_scanFolderButton);

    m_scanDriveButton = new QPushButton(tr("Scan Drive"), this);
    m_scanDriveButton->setObjectName(QStringLiteral("fragmentationScanDriveButton"));
    toolbarRow->addWidget(m_scanDriveButton);

    toolbarRow->addWidget(new QLabel(tr("Minimum file size:"), this));

    m_minSizeSpinBox = new QSpinBox(this);
    m_minSizeSpinBox->setRange(0, 999999);
    // Higher default than Duplicate Finder's 4 KB -- a file at or below one NTFS cluster (commonly
    // 4 KB) can never be fragmented, so this cuts noise from the large number of small files that
    // are never meaningfully fragmented. The user can lower it to 0.
    m_minSizeSpinBox->setValue(64);
    toolbarRow->addWidget(m_minSizeSpinBox);

    m_minSizeUnitCombo = new QComboBox(this);
    m_minSizeUnitCombo->addItems({ tr("B"), tr("KB"), tr("MB") });
    m_minSizeUnitCombo->setCurrentIndex(1); // KB -- 64 KB default together with the spinbox above.
    toolbarRow->addWidget(m_minSizeUnitCombo);

    m_showOnlyFragmentedCheckBox = new QCheckBox(tr("Show only fragmented files"), this);
    m_showOnlyFragmentedCheckBox->setChecked(true);
    toolbarRow->addWidget(m_showOnlyFragmentedCheckBox);

    toolbarRow->addStretch(1);
    mainLayout->addLayout(toolbarRow);

    m_summaryLabel = new QLabel(tr("No scan yet for this folder"), this);
    m_summaryLabel->setObjectName(QStringLiteral("fragmentationSummaryLabel"));
    mainLayout->addWidget(m_summaryLabel);

    m_resultsModel = new FragmentationResultsModel(this);
    m_resultsView = new QTreeView(this);
    m_resultsView->setObjectName(QStringLiteral("fragmentationResultsView"));
    m_resultsView->setModel(m_resultsModel);
    m_resultsView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultsView->setUniformRowHeights(true);
    m_resultsView->setRootIsDecorated(false);
    m_resultsView->header()->setStretchLastSection(true);
    m_resultsView->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_resultsView->header()->resizeSection(FragmentationResultsModel::NameColumn, 240);
    m_resultsView->header()->resizeSection(FragmentationResultsModel::FolderColumn, 320);
    m_resultsView->setSortingEnabled(true);
    m_resultsView->sortByColumn(FragmentationResultsModel::FragmentsColumn, Qt::DescendingOrder);
    mainLayout->addWidget(m_resultsView, 1);

    connect(m_scanFolderButton, &QPushButton::clicked, this, [this]() { m_viewModel->scanFolder(minFileSizeBytes()); });
    connect(m_scanDriveButton, &QPushButton::clicked, this, [this]() { m_viewModel->scanDrive(minFileSizeBytes()); });
    connect(m_showOnlyFragmentedCheckBox, &QCheckBox::toggled, m_resultsModel, &FragmentationResultsModel::setShowOnlyFragmented);

    connect(m_viewModel, &FragmentationViewModel::scanningChanged, this, &FragmentationWidget::onScanningChanged);
    connect(m_viewModel, &FragmentationViewModel::resultsChanged, this, &FragmentationWidget::onResultsChanged);
    connect(m_viewModel, &FragmentationViewModel::openContainingFolderRequested, this,
            &FragmentationWidget::openContainingFolderRequested);
    // Previously unconnected -- a scan failure (e.g. an I/O error partway through a whole-drive
    // recursive walk) terminated the progress notification and silently left the results view
    // exactly as it was, with no indication anything went wrong. Mirrors
    // DuplicateFinderDialog::onScanFinished's error popup.
    connect(m_viewModel, &FragmentationViewModel::scanFailed, this, [&notificationCenter](const QString& message) {
        notificationCenter.showPopup(NotificationType::Error, tr("Fragmentation Scan Failed"), message);
    });
    connect(m_resultsView, &QTreeView::customContextMenuRequested, this, &FragmentationWidget::showResultsContextMenu);

    connect(m_tab, &TabViewModel::currentPathChanged, this, [this](const std::filesystem::path&) { updateButtonsEnabled(); });

    m_resultsModel->setShowOnlyFragmented(m_showOnlyFragmentedCheckBox->isChecked());
    updateButtonsEnabled();
}

void FragmentationWidget::updateButtonsEnabled()
{
    const std::filesystem::path path = m_tab->currentPath();
    const bool validRoot = VirtualPaths::isValidScanRoot(path);

    const bool scanning = m_viewModel->isScanning();
    m_scanFolderButton->setEnabled(validRoot && !scanning);
    m_scanDriveButton->setEnabled(validRoot && !scanning);
}

void FragmentationWidget::onScanningChanged(bool scanning)
{
    m_scanFolderButton->setText(scanning ? tr("Scanning...") : tr("Scan Folder"));
    m_scanDriveButton->setText(scanning ? tr("Scanning...") : tr("Scan Drive"));
    updateButtonsEnabled();
}

void FragmentationWidget::onResultsChanged(const std::vector<FileFragmentationInfo>& files, const FragmentationSummary& summary)
{
    m_resultsModel->setFiles(files);

    if (files.empty())
    {
        m_summaryLabel->setText(tr("No scan yet for this folder"));
        return;
    }

    const int fragmentedPercent =
        summary.filesScanned > 0
            ? static_cast<int>(100.0 * static_cast<double>(summary.fragmentedFileCount) / static_cast<double>(summary.filesScanned) + 0.5)
            : 0;

    m_summaryLabel->setText(tr("Files scanned: %1  |  Total size scanned: %2  |  Fragmented files: %3 (%4%)  |  Total fragments: %5")
                                 .arg(summary.filesScanned)
                                 .arg(FileSizeFormat::format(summary.totalBytesScanned))
                                 .arg(summary.fragmentedFileCount)
                                 .arg(fragmentedPercent)
                                 .arg(summary.totalFragments));
}

void FragmentationWidget::showResultsContextMenu(const QPoint& pos)
{
    const QModelIndex index = m_resultsView->indexAt(pos);
    if (!index.isValid())
    {
        return;
    }

    const std::filesystem::path path = m_resultsModel->pathAt(index.row());

    QMenu menu(this);
    menu.addAction(tr("Open Containing Folder"), this, [this, path]() { m_viewModel->requestOpenContainingFolder(path); });
    menu.exec(m_resultsView->viewport()->mapToGlobal(pos));
}

std::uintmax_t FragmentationWidget::minFileSizeBytes() const
{
    return minFileSizeBytesFrom(m_minSizeUnitCombo, m_minSizeSpinBox);
}
