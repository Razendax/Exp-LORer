#include "TagSearchEdit.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QListWidget>

namespace
{
    constexpr int kMaxVisibleRows = 8;
}

TagSearchEdit::TagSearchEdit(QWidget* parent)
    : QLineEdit(parent)
{
    m_popup = new QListWidget(this);
    m_popup->setObjectName(QStringLiteral("tagPanelSearchEditPopup"));
    m_popup->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_popup->setFocusPolicy(Qt::NoFocus);
    m_popup->setSelectionMode(QAbstractItemView::SingleSelection);
    m_popup->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_popup->hide();

    connect(m_popup, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        m_popup->setCurrentItem(item);
        acceptPendingCompletion();
    });
}

void TagSearchEdit::setSuggestions(const QStringList& tagNames)
{
    m_suggestions = tagNames;
    refreshPopup();
}

void TagSearchEdit::refreshPopup()
{
    if (!hasFocus() || text().isEmpty() || m_suggestions.isEmpty())
    {
        hidePopup();
        return;
    }

    m_popup->clear();
    for (const QString& name : m_suggestions)
    {
        new QListWidgetItem(name, m_popup);
    }
    m_popup->setCurrentRow(-1);

    const int visibleRows = std::min(static_cast<int>(m_suggestions.size()), kMaxVisibleRows);
    const int rowHeight = m_popup->sizeHintForRow(0);
    m_popup->resize(width(), rowHeight * visibleRows + 2 * m_popup->frameWidth());
    m_popup->move(mapToGlobal(QPoint(0, height())));
    m_popup->show();

    applyInlineCompletion();
}

void TagSearchEdit::hidePopup()
{
    m_popup->hide();
}

void TagSearchEdit::applyInlineCompletion()
{
    if (m_lastEditWasDeletion || m_suggestions.isEmpty())
    {
        return;
    }

    // searchResults() can rank in a substring-only match first for a short query, so only ghost a
    // suffix when the top suggestion actually extends the typed text as a prefix.
    const QString& first = m_suggestions.first();
    if (!first.startsWith(text(), Qt::CaseInsensitive) || first.length() <= text().length())
    {
        return;
    }

    const QString suffix = first.mid(text().length());
    const int insertPos = text().length();
    setText(text() + suffix);
    setSelection(insertPos, suffix.length());
}

bool TagSearchEdit::acceptPendingCompletion()
{
    if (m_popup->isVisible() && m_popup->currentRow() >= 0)
    {
        setText(m_popup->currentItem()->text());
        deselect();
        setCursorPosition(text().length());
        hidePopup();
        return true;
    }

    if (hasSelectedText())
    {
        // A ghosted inline-completion suffix is already in the text, selected -- commit it in
        // place (deselect) rather than assigning anything.
        deselect();
        setCursorPosition(text().length());
        hidePopup();
        return true;
    }

    if (m_popup->isVisible() && m_popup->count() > 0)
    {
        // Substring-only match (the top suggestion didn't extend the typed text as a prefix, so
        // nothing got ghosted): fall back to the first, top-ranked suggestion.
        setText(m_popup->item(0)->text());
        setCursorPosition(text().length());
        hidePopup();
        return true;
    }

    return false;
}

void TagSearchEdit::moveHighlight(int delta)
{
    const int count = m_popup->count();
    if (count == 0)
    {
        return;
    }

    const int currentRow = m_popup->currentRow();
    const int row = (currentRow < 0) ? (delta > 0 ? 0 : count - 1) : std::clamp(currentRow + delta, 0, count - 1);
    m_popup->setCurrentRow(row);
    m_popup->scrollToItem(m_popup->currentItem());
}

void TagSearchEdit::keyPressEvent(QKeyEvent* event)
{
    const bool popupVisible = m_popup->isVisible();

    switch (event->key())
    {
        case Qt::Key_Down:
            if (popupVisible)
            {
                moveHighlight(1);
                event->accept();
                return;
            }
            break;

        case Qt::Key_Up:
            if (popupVisible)
            {
                moveHighlight(-1);
                event->accept();
                return;
            }
            break;

        case Qt::Key_Escape:
            if (popupVisible)
            {
                hidePopup();
                event->accept();
                return;
            }
            break;

        default:
            break;
    }

    // Return/Enter deliberately *not* intercepted here, unlike AddressBarWidget: this input's
    // Enter must always act on the literal typed text (assign-if-exists / create-then-assign),
    // never on whatever's highlighted in the popup -- it falls straight through to QLineEdit's
    // default handling, which just emits the inherited returnPressed() signal. Tab is the only way
    // to pull a suggestion into the text box.

    m_lastEditWasDeletion = (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete);

    QLineEdit::keyPressEvent(event);
}

bool TagSearchEdit::event(QEvent* ev)
{
    // A bare Tab is claimed for focus-chain traversal by QWidget::event() *before* keyPressEvent is
    // ever called, so keyPressEvent alone can never intercept it -- it has to be handled here.
    if (ev->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(ev);
        if (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() == Qt::NoModifier)
        {
            if (acceptPendingCompletion())
            {
                return true;
            }
        }
    }

    return QLineEdit::event(ev);
}

void TagSearchEdit::focusOutEvent(QFocusEvent* event)
{
    hidePopup();
    QLineEdit::focusOutEvent(event);
}
