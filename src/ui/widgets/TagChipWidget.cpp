#include "TagChipWidget.h"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
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
    , m_kind(kind)
{
    // Needed for a plain QWidget subclass to paint a stylesheet background/border at all.
    setAttribute(Qt::WA_StyledBackground);

    m_label = new QLabel(QString::fromStdString(m_tag.name()), this);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* layout = new QHBoxLayout(this);
    // Right margin is smaller when a trailing button follows so the pill's own padding plus the
    // button's padding don't stack into a lopsided gap; ReadOnly/Selectable chips stay symmetric.
    const bool hasTrailingButton = kind == Kind::Addable || kind == Kind::Removable;
    layout->setContentsMargins(10, 3, hasTrailingButton ? 4 : 10, 3);
    layout->setSpacing(2);
    layout->addWidget(m_label);

    if (kind == Kind::Addable)
    {
        auto* addButton = new QToolButton(this);
        addButton->setText(QStringLiteral("+"));
        addButton->setToolTip(tr("Add to selection"));
        addButton->setAutoRaise(true);
        addButton->setCursor(Qt::PointingHandCursor);
        connect(addButton, &QToolButton::clicked, this, [this]() { emit addClicked(m_tag.id()); });
        layout->addWidget(addButton);
    }
    else if (kind == Kind::Removable)
    {
        auto* removeButton = new QToolButton(this);
        removeButton->setText(QStringLiteral("x"));
        removeButton->setToolTip(tr("Remove from selection"));
        removeButton->setAutoRaise(true);
        removeButton->setCursor(Qt::PointingHandCursor);
        connect(removeButton, &QToolButton::clicked, this, [this]() { emit removeClicked(m_tag.id()); });
        layout->addWidget(removeButton);
    }

    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    updateChipStyle();
}

void TagChipWidget::setSelected(bool selected)
{
    if (m_selected == selected)
    {
        return;
    }
    m_selected = selected;
    updateChipStyle();
}

void TagChipWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit clicked(m_tag.id());
        return;
    }
    QWidget::mousePressEvent(event);
}

void TagChipWidget::updateChipStyle()
{
    const QColor background(QString::fromStdString(m_tag.hexColor()));
    const QString textColor = textColorFor(background);
    const QString borderColor = m_selected ? QStringLiteral("#1A73E8") : QStringLiteral("rgba(0, 0, 0, 60)");
    const QString borderWidth = m_selected ? QStringLiteral("2px") : QStringLiteral("1px");

    // The pill background/border lives on the chip widget itself (not the label) so the +/x
    // button, which shares this widget's layout, renders inside the same rounded pill instead of
    // as a separate square control next to it. QLabel/QToolButton children are styled transparent
    // via descendant selectors scoped to this widget so they don't paint their own background.
    setStyleSheet(QStringLiteral(
                      "TagChipWidget { background-color: %1; border: %2 solid %3; border-radius: 11px; }"
                      "TagChipWidget QLabel { background: transparent; border: none; color: %4; }"
                      "TagChipWidget QToolButton { background: transparent; border: none; color: %4; "
                      "font-weight: bold; padding: 0px 2px; }"
                      "TagChipWidget QToolButton:hover { background-color: rgba(0, 0, 0, 40); border-radius: 8px; }")
                      .arg(background.name(), borderWidth, borderColor, textColor));
}
