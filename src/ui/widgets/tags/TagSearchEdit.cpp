#include "TagSearchEdit.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QSignalBlocker>

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
    {
        // Ghosting the suffix must not feed back into the search query via textChanged -- otherwise
        // the viewmodel would re-narrow searchResults down to just this one tag (since the ghosted
        // text now matches only itself), and the next setSuggestions() would collapse the popup to
        // a single item instead of showing every match for what the user actually typed.
        const QSignalBlocker blocker(this);
        setText(text() + suffix);
    }
    setSelection(insertPos, suffix.length());
}

bool TagSearchEdit::acceptPendingCompletion()
{
    bool committed = false;

    if (m_popup->isVisible() && m_popup->currentRow() >= 0)
    {
        setText(m_popup->currentItem()->text());
        deselect();
        setCursorPosition(text().length());
        committed = true;
    }
    else if (hasSelectedText())
    {
        // A ghosted inline-completion suffix is already in the text, selected -- commit it in
        // place (deselect) rather than assigning anything.
        deselect();
        setCursorPosition(text().length());
        committed = true;
    }
    else if (m_popup->isVisible() && m_popup->count() > 0)
    {
        // Substring-only match (the top suggestion didn't extend the typed text as a prefix, so
        // nothing got ghosted): fall back to the first, top-ranked suggestion.
        setText(m_popup->item(0)->text());
        setCursorPosition(text().length());
        committed = true;
    }

    if (!committed)
    {
        return false;
    }

    hidePopup();

    // applyInlineCompletion() blocks textChanged while ghosting (so the search query it feeds
    // doesn't self-narrow to the ghosted text), and setText() above is a no-op when the committed
    // text already matches what's on screen (accepting the very suggestion that was already
    // ghosted) -- Qt then elides textChanged entirely. Either way the viewmodel's search query can
    // be left stuck at the pre-ghost prefix, which would make the caller's return-key handling
    // create a new tag from that stale prefix instead of acting on the tag actually committed here.
    // Force the signal so the query always matches what's in the box after a commit.
    emit textChanged(text());
    return true;
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

        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (popupVisible && m_popup->currentRow() >= 0)
            {
                // The user navigated to a popup row with Up/Down -- commit it into the text first,
                // then fall through to QLineEdit's default handling so returnPressed() still fires,
                // now acting on the selected tag instead of the literal typed text.
                acceptPendingCompletion();
            }
            break;

        default:
            break;
    }

    // Return/Enter otherwise (no explicit popup row highlighted) falls straight through to
    // QLineEdit's default handling, which just emits the inherited returnPressed() signal, acting
    // on the literal typed text (assign-if-exists / create-then-assign) -- it never accepts an
    // unaccepted ghosted suffix. Tab is the other way to pull a suggestion into the text box.

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
