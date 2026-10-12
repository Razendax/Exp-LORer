#include "MediaViewerSortViewBar.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QSignalBlocker>

MediaViewerSortViewBar::MediaViewerSortViewBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mediaViewerSortViewBar"));

    m_sortCriterionCombo = new QComboBox(this);
    m_sortCriterionCombo->setObjectName(QStringLiteral("mediaViewerSortCriterionCombo"));
    m_sortCriterionCombo->addItem(tr("Name"));
    m_sortCriterionCombo->addItem(tr("Duration"));
    m_sortCriterionCombo->addItem(tr("Size"));
    m_sortCriterionCombo->setToolTip(tr("Sort by"));

    m_sortDirectionCombo = new QComboBox(this);
    m_sortDirectionCombo->setObjectName(QStringLiteral("mediaViewerSortDirectionCombo"));
    m_sortDirectionCombo->addItem(tr("Ascending"));
    m_sortDirectionCombo->addItem(tr("Descending"));
    m_sortDirectionCombo->setToolTip(tr("Sort direction"));

    m_viewModeCombo = new QComboBox(this);
    m_viewModeCombo->setObjectName(QStringLiteral("mediaViewerViewModeCombo"));
    m_viewModeCombo->addItem(tr("Thumbnails"));
    m_viewModeCombo->addItem(tr("Filmstrip"));
    m_viewModeCombo->addItem(tr("Details"));
    m_viewModeCombo->setToolTip(tr("View"));

    m_groupModeCombo = new QComboBox(this);
    m_groupModeCombo->setObjectName(QStringLiteral("mediaViewerGroupModeCombo"));
    m_groupModeCombo->addItem(tr("Recursive"));
    m_groupModeCombo->addItem(tr("Folder"));
    m_groupModeCombo->setToolTip(tr("Group"));

    m_previewModeCombo = new QComboBox(this);
    m_previewModeCombo->setObjectName(QStringLiteral("mediaViewerPreviewModeCombo"));
    m_previewModeCombo->addItem(tr("Static"));
    m_previewModeCombo->addItem(tr("Animate"));
    m_previewModeCombo->addItem(tr("Autoplay"));
    m_previewModeCombo->setToolTip(tr("Preview"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_sortCriterionCombo);
    layout->addWidget(m_sortDirectionCombo);
    layout->addWidget(m_viewModeCombo);
    layout->addWidget(m_groupModeCombo);
    layout->addWidget(m_previewModeCombo);

    connect(m_sortCriterionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto criterion = index == 1 ? MediaViewerSortCriterion::Duration
                                : index == 2 ? MediaViewerSortCriterion::Size
                                             : MediaViewerSortCriterion::Name;
        emit sortCriterionChanged(criterion);
    });
    connect(m_sortDirectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { emit sortAscendingChanged(index == 0); });
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto mode = index == 1 ? MediaViewerViewMode::Filmstrip
                           : index == 2 ? MediaViewerViewMode::Details
                                        : MediaViewerViewMode::Thumbnails;
        emit viewModeChanged(mode);
    });
    connect(m_groupModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        emit groupModeChanged(index == 1 ? MediaViewerGroupMode::ByFolder : MediaViewerGroupMode::Recursive);
    });
    connect(m_previewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto mode = index == 1 ? MediaViewerPreviewMode::AnimateOnHover
                           : index == 2 ? MediaViewerPreviewMode::Autoplay
                                        : MediaViewerPreviewMode::Static;
        emit previewModeChanged(mode);
    });
}

void MediaViewerSortViewBar::setSortCriterion(MediaViewerSortCriterion criterion)
{
    const QSignalBlocker blocker(m_sortCriterionCombo);
    switch (criterion)
    {
    case MediaViewerSortCriterion::Name:
        m_sortCriterionCombo->setCurrentIndex(0);
        break;
    case MediaViewerSortCriterion::Duration:
        m_sortCriterionCombo->setCurrentIndex(1);
        break;
    case MediaViewerSortCriterion::Size:
        m_sortCriterionCombo->setCurrentIndex(2);
        break;
    }
}

void MediaViewerSortViewBar::setSortAscending(bool ascending)
{
    const QSignalBlocker blocker(m_sortDirectionCombo);
    m_sortDirectionCombo->setCurrentIndex(ascending ? 0 : 1);
}

void MediaViewerSortViewBar::setViewMode(MediaViewerViewMode mode)
{
    const QSignalBlocker blocker(m_viewModeCombo);
    switch (mode)
    {
    case MediaViewerViewMode::Thumbnails:
        m_viewModeCombo->setCurrentIndex(0);
        break;
    case MediaViewerViewMode::Filmstrip:
        m_viewModeCombo->setCurrentIndex(1);
        break;
    case MediaViewerViewMode::Details:
        m_viewModeCombo->setCurrentIndex(2);
        break;
    }
}

void MediaViewerSortViewBar::setGroupMode(MediaViewerGroupMode mode)
{
    const QSignalBlocker blocker(m_groupModeCombo);
    m_groupModeCombo->setCurrentIndex(mode == MediaViewerGroupMode::ByFolder ? 1 : 0);
}

void MediaViewerSortViewBar::setPreviewMode(MediaViewerPreviewMode mode)
{
    const QSignalBlocker blocker(m_previewModeCombo);
    switch (mode)
    {
    case MediaViewerPreviewMode::Static:
        m_previewModeCombo->setCurrentIndex(0);
        break;
    case MediaViewerPreviewMode::AnimateOnHover:
        m_previewModeCombo->setCurrentIndex(1);
        break;
    case MediaViewerPreviewMode::Autoplay:
        m_previewModeCombo->setCurrentIndex(2);
        break;
    }
}
