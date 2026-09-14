#include "TagChipWidget.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace
{
    // Picks readable text (black/white) against the chip's background color for contrast.
    QString textColorFor(const QColor& background)
    {
        const double luminance = 0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue();
        return luminance > 140 ? QStringLiteral("#000000") : QStringLiteral("#FFFFFF");
    }
}

TagChipWidget::TagChipWidget(Tag tag, Kind kind, QWidget* parent)
    : QWidget(parent)
    , m_tag(std::move(tag))
{
    const QColor background(QString::fromStdString(m_tag.hexColor()));

    auto* label = new QLabel(QString::fromStdString(m_tag.name()), this);
    label->setStyleSheet(QStringLiteral("QLabel { background-color: %1; color: %2; border-radius: 8px; padding: 2px 8px; }")
                              .arg(background.name(), textColorFor(background)));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(label);

    if (kind == Kind::Addable)
    {
        auto* addButton = new QToolButton(this);
        addButton->setText(QStringLiteral("+"));
        addButton->setToolTip(tr("Add to selection"));
        connect(addButton, &QToolButton::clicked, this, [this]() { emit addClicked(m_tag.id()); });
        layout->addWidget(addButton);
    }
    else if (kind == Kind::Removable)
    {
        auto* removeButton = new QToolButton(this);
        removeButton->setText(QStringLiteral("x"));
        removeButton->setToolTip(tr("Remove from selection"));
        connect(removeButton, &QToolButton::clicked, this, [this]() { emit removeClicked(m_tag.id()); });
        layout->addWidget(removeButton);
    }
}
