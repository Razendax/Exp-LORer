#include "SearchCriteriaPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

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

    auto* layout = new QHBoxLayout(this);
    layout->addWidget(new QLabel(tr("Name:"), this));
    layout->addWidget(m_nameEdit, 2);
    layout->addWidget(new QLabel(tr("Min size:"), this));
    layout->addWidget(m_minSizeKbSpin);
    layout->addWidget(new QLabel(tr("Max size:"), this));
    layout->addWidget(m_maxSizeKbSpin);
    layout->addWidget(new QLabel(tr("Extension:"), this));
    layout->addWidget(m_extensionEdit, 1);
    layout->addWidget(m_searchButton);
    layout->addWidget(m_closeButton);

    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this]() { emit searchRequested(currentCriteria()); });
    connect(m_searchButton, &QPushButton::clicked, this, [this]() { emit searchRequested(currentCriteria()); });
    connect(m_closeButton, &QToolButton::clicked, this, &SearchCriteriaPanel::closeRequested);

    connect(m_nameEdit, &QLineEdit::textEdited, this, &SearchCriteriaPanel::onAnyFieldEdited);
    connect(m_minSizeKbSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SearchCriteriaPanel::onAnyFieldEdited);
    connect(m_maxSizeKbSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &SearchCriteriaPanel::onAnyFieldEdited);
    connect(m_extensionEdit, &QLineEdit::textEdited, this, &SearchCriteriaPanel::onAnyFieldEdited);
}

void SearchCriteriaPanel::setCriteria(const SearchCriteria& criteria)
{
    const QSignalBlocker nameBlocker(m_nameEdit);
    const QSignalBlocker minBlocker(m_minSizeKbSpin);
    const QSignalBlocker maxBlocker(m_maxSizeKbSpin);
    const QSignalBlocker extensionBlocker(m_extensionEdit);

    m_nameEdit->setText(QString::fromStdString(criteria.nameQuery));
    m_minSizeKbSpin->setValue(criteria.minSizeBytes ? static_cast<int>(*criteria.minSizeBytes / kBytesPerKb) : 0);
    m_maxSizeKbSpin->setValue(criteria.maxSizeBytes ? static_cast<int>(*criteria.maxSizeBytes / kBytesPerKb) : 0);
    m_extensionEdit->setText(QString::fromStdString(criteria.extensionList));
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
    return criteria;
}

void SearchCriteriaPanel::onAnyFieldEdited()
{
    emit criteriaEdited(currentCriteria());
}
