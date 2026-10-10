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
    m_viewModeCombo->setToolTip(tr("View"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_sortCriterionCombo);
    layout->addWidget(m_sortDirectionCombo);
    layout->addWidget(m_viewModeCombo);

    connect(m_sortCriterionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const auto criterion = index == 1 ? MediaViewerSortCriterion::Duration
                                : index == 2 ? MediaViewerSortCriterion::Size
                                             : MediaViewerSortCriterion::Name;
        emit sortCriterionChanged(criterion);
    });
    connect(m_sortDirectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { emit sortAscendingChanged(index == 0); });
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { emit viewModeChanged(MediaViewerViewMode::Thumbnails); });
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
    }
}
