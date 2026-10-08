#include "FolderSizeWidget.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "ArchiveExtensions.h"
#include "FolderSizeViewModel.h"
#include "NotificationCenter.h"
#include "PieChartWidget.h"
#include "TabViewModel.h"
#include "VirtualPaths.h"

FolderSizeWidget::FolderSizeWidget(DiskUsageService& service, NotificationCenter& notificationCenter, TabViewModel* tab,
                                    QWidget* parent)
    : QWidget(parent)
    , m_tab(tab)
{
    m_viewModel = new FolderSizeViewModel(service, notificationCenter, tab, this);

    auto* mainLayout = new QHBoxLayout(this);

    m_pieChart = new PieChartWidget(this);
    m_pieChart->setObjectName(QStringLiteral("folderSizePieChart"));
    mainLayout->addWidget(m_pieChart, 1);

    auto* buttonColumn = new QVBoxLayout();
    m_scanFolderButton = new QPushButton(tr("Scan Folder"), this);
    m_scanFolderButton->setObjectName(QStringLiteral("folderSizeScanFolderButton"));
    buttonColumn->addWidget(m_scanFolderButton);

    m_scanDriveButton = new QPushButton(tr("Scan Drive"), this);
    m_scanDriveButton->setObjectName(QStringLiteral("folderSizeScanDriveButton"));
    buttonColumn->addWidget(m_scanDriveButton);

    buttonColumn->addStretch(1);
    mainLayout->addLayout(buttonColumn);

    connect(m_scanFolderButton, &QPushButton::clicked, m_viewModel, &FolderSizeViewModel::scanFolder);
    connect(m_scanDriveButton, &QPushButton::clicked, m_viewModel, &FolderSizeViewModel::scanDrive);

    connect(m_viewModel, &FolderSizeViewModel::scanningChanged, this, &FolderSizeWidget::onScanningChanged);
    connect(m_viewModel, &FolderSizeViewModel::dataChanged, this, &FolderSizeWidget::onDataChanged);
    connect(m_viewModel, &FolderSizeViewModel::navigationRequested, tab, &TabViewModel::navigateTo);
    connect(m_pieChart, &PieChartWidget::segmentClicked, m_viewModel, &FolderSizeViewModel::onSegmentClicked);
    // Previously unconnected -- a scan failure (e.g. an I/O error partway through a whole-drive
    // recursive walk) silently left the pie chart exactly as it was, with no indication anything
    // went wrong. Mirrors FragmentationWidget's identical fix.
    connect(m_viewModel, &FolderSizeViewModel::scanFailed, this, [&notificationCenter](const QString& message) {
        notificationCenter.showPopup(NotificationType::Error, tr("Folder Size Scan Failed"), message);
    });

    connect(m_tab, &TabViewModel::currentPathChanged, this, [this](const std::filesystem::path&) { updateButtonsEnabled(); });

    updateButtonsEnabled();
}

void FolderSizeWidget::updateButtonsEnabled()
{
    const std::filesystem::path path = m_tab->currentPath();
    const bool validRoot = !path.empty() && path != VirtualPaths::ThisPC && !ArchiveExtensions::archiveAncestorInPath(path);

    const bool scanning = m_viewModel->isScanning();
    m_scanFolderButton->setEnabled(validRoot && !scanning);
    m_scanDriveButton->setEnabled(validRoot && !scanning);
}

void FolderSizeWidget::onScanningChanged(bool scanning)
{
    m_scanFolderButton->setText(scanning ? tr("Scanning...") : tr("Scan Folder"));
    m_scanDriveButton->setText(scanning ? tr("Scanning...") : tr("Scan Drive"));
    updateButtonsEnabled();
}

void FolderSizeWidget::onDataChanged(const std::filesystem::path& folder, const std::vector<DiskUsageNode>& topChildren,
                                      std::uintmax_t totalSize)
{
    m_pieChart->setData(folder, topChildren, totalSize);
}
