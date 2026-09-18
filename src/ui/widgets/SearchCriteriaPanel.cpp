#include "SearchCriteriaPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "FlowLayout.h"
#include "TagChipWidget.h"

namespace
{
    constexpr int kMaxSizeKb = 2000000000;
    constexpr std::uintmax_t kBytesPerKb = 1024;
}

SearchCriteriaPanel::SearchCriteriaPanel(QWidget* parent)
    : QWidget(parent)
{
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("Name contains..."));

    m_minSizeKbSpin = new QSpinBox(this);
    m_minSizeKbSpin->setRange(0, kMaxSizeKb);
    m_minSizeKbSpin->setSpecialValueText(tr("Any"));
    m_minSizeKbSpin->setSuffix(tr(" KB"));

    m_maxSizeKbSpin = new QSpinBox(this);
    m_maxSizeKbSpin->setRange(0, kMaxSizeKb);
    m_maxSizeKbSpin->setSpecialValueText(tr("Any"));
    m_maxSizeKbSpin->setSuffix(tr(" KB"));

    m_extensionEdit = new QLineEdit(this);
    m_extensionEdit->setPlaceholderText(tr("jpg, png..."));

    m_searchButton = new QPushButton(tr("Search"), this);

    m_closeButton = new QToolButton(this);
    m_closeButton->setText(QStringLiteral("×"));
    m_closeButton->setToolTip(tr("Close advanced search"));

    auto* fieldsRow = new QHBoxLayout();
    fieldsRow->addWidget(new QLabel(tr("Name:"), this));
    fieldsRow->addWidget(m_nameEdit, 2);
    fieldsRow->addWidget(new QLabel(tr("Min size:"), this));
    fieldsRow->addWidget(m_minSizeKbSpin);
    fieldsRow->addWidget(new QLabel(tr("Max size:"), this));
    fieldsRow->addWidget(m_maxSizeKbSpin);
    fieldsRow->addWidget(new QLabel(tr("Extension:"), this));
    fieldsRow->addWidget(m_extensionEdit, 1);
    fieldsRow->addWidget(m_searchButton);
    fieldsRow->addWidget(m_closeButton);

    m_tagSearchEdit = new QLineEdit(this);
    m_tagSearchEdit->setPlaceholderText(tr("Search tags..."));
    m_tagMatchesLayout = new FlowLayout();

    auto* tagMatchesRow = new QHBoxLayout();
    tagMatchesRow->addWidget(m_tagSearchEdit);
    tagMatchesRow->addLayout(m_tagMatchesLayout, 1);

    m_tagCriteriaLayout = new FlowLayout();
    auto* tagCriteriaRow = new QHBoxLayout();
    tagCriteriaRow->addWidget(new QLabel(tr("Tag search criteria:"), this));
    tagCriteriaRow->addLayout(m_tagCriteriaLayout, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(fieldsRow);
    layout->addLayout(tagMatchesRow);
    layout->addLayout(tagCriteriaRow);

    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this]() { emit searchRequested(currentCriteria()); });
    connect(m_searchButton, &QPushButton::clicked, this, [this]() { emit searchRequested(currentCriteria()); });
    connect(m_closeButton, &QToolButton::clicked, this, &SearchCriteriaPanel::closeRequested);

    connect(m_nameEdit, &QLineEdit::textEdited, this, &SearchCriteriaPanel::onAnyFieldEdited);
    connect(m_minSizeKbSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SearchCriteriaPanel::onAnyFieldEdited);
    connect(m_maxSizeKbSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SearchCriteriaPanel::onAnyFieldEdited);
    connect(m_extensionEdit, &QLineEdit::textEdited, this, &SearchCriteriaPanel::onAnyFieldEdited);

    connect(m_tagSearchEdit, &QLineEdit::textChanged, this, &SearchCriteriaPanel::tagSearchQueryRequested);
}

void SearchCriteriaPanel::setCriteria(const SearchCriteria& criteria, const std::vector<Tag>& criteriaTags)
{
    const QSignalBlocker nameBlocker(m_nameEdit);
    const QSignalBlocker minBlocker(m_minSizeKbSpin);
    const QSignalBlocker maxBlocker(m_maxSizeKbSpin);
    const QSignalBlocker extensionBlocker(m_extensionEdit);

    m_nameEdit->setText(QString::fromStdString(criteria.nameQuery));
    m_minSizeKbSpin->setValue(criteria.minSizeBytes ? static_cast<int>(*criteria.minSizeBytes / kBytesPerKb) : 0);
    m_maxSizeKbSpin->setValue(criteria.maxSizeBytes ? static_cast<int>(*criteria.maxSizeBytes / kBytesPerKb) : 0);
    m_extensionEdit->setText(QString::fromStdString(criteria.extensionList));
    m_lastKnownTagIds = criteria.tagIds;

    clearLayout(m_tagCriteriaLayout);
    for (const Tag& tag : criteriaTags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::Removable);
        connect(chip, &TagChipWidget::removeClicked, this, &SearchCriteriaPanel::tagCriterionRemoved);
        m_tagCriteriaLayout->addWidget(chip);
    }
}

void SearchCriteriaPanel::setTagSearchResults(const std::vector<Tag>& tags)
{
    clearLayout(m_tagMatchesLayout);
    for (const Tag& tag : tags)
    {
        auto* chip = new TagChipWidget(tag, TagChipWidget::Kind::ReadOnly);
        connect(chip, &TagChipWidget::clicked, this, &SearchCriteriaPanel::tagCriterionAdded);
        m_tagMatchesLayout->addWidget(chip);
    }
}

void SearchCriteriaPanel::clearLayout(FlowLayout* layout)
{
    while (layout->count() > 0)
    {
        QLayoutItem* item = layout->takeAt(0);
        if (QWidget* widget = item->widget())
        {
            delete widget;
        }
        delete item;
    }
}

SearchCriteria SearchCriteriaPanel::currentCriteria() const
{
    SearchCriteria criteria;
    criteria.nameQuery = m_nameEdit->text().trimmed().toStdString();
    if (m_minSizeKbSpin->value() > 0)
    {
        criteria.minSizeBytes = static_cast<std::uintmax_t>(m_minSizeKbSpin->value()) * kBytesPerKb;
    }
    if (m_maxSizeKbSpin->value() > 0)
    {
        criteria.maxSizeBytes = static_cast<std::uintmax_t>(m_maxSizeKbSpin->value()) * kBytesPerKb;
    }
    criteria.extensionList = m_extensionEdit->text().trimmed().toStdString();
    criteria.tagIds = m_lastKnownTagIds;
    return criteria;
}

void SearchCriteriaPanel::onAnyFieldEdited()
{
    emit criteriaEdited(currentCriteria());
}
