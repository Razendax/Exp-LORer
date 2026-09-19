#include "AddressBarWidget.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QEvent>
#include <QFocusEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QListWidget>
#include <QStyle>
#include <QTimer>

namespace
{
    constexpr int kDebounceMs = 120;
    constexpr int kMaxVisibleRows = 8;
}

AddressBarWidget::AddressBarWidget(QWidget* parent)
    : QLineEdit(parent)
{
    m_popup = new QListWidget(this);
    m_popup->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    m_popup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_popup->setFocusPolicy(Qt::NoFocus);
    m_popup->setSelectionMode(QAbstractItemView::SingleSelection);
    m_popup->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_popup->hide();

    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(kDebounceMs);
    connect(m_debounceTimer, &QTimer::timeout, this, &AddressBarWidget::requestSuggestionsIfNeeded);

    connect(this, &QLineEdit::textEdited, this, &AddressBarWidget::onTextEdited);
    connect(m_popup, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        m_popup->setCurrentItem(item);
        acceptCurrentSuggestion();
    });
}

void AddressBarWidget::onTextEdited(const QString& text)
{
    const std::filesystem::path directory = directoryPortion(text);
    if (directory.empty())
    {
        m_debounceTimer->stop();
        hidePopup();
        return;
    }

    if (m_haveCachedDirectory && directory == m_cachedDirectory)
    {
        m_debounceTimer->stop();
        refreshPopup();
        return;
    }

    m_pendingDirectory = directory;
    m_debounceTimer->start();
}

void AddressBarWidget::requestSuggestionsIfNeeded()
{
    emit folderSuggestionsRequested(m_pendingDirectory);
}

void AddressBarWidget::setSuggestions(const std::filesystem::path& directory, const QStringList& folderNames)
{
    if (directory != directoryPortion(text()))
    {
        return; // stale response -- the directory portion of the text has since changed again
    }

    m_cachedDirectory = directory;
    m_cachedFolderNames = folderNames;
    m_haveCachedDirectory = true;

    refreshPopup();
}

void AddressBarWidget::refreshPopup()
{
    if (!m_haveCachedDirectory)
    {
        hidePopup();
        return;
    }

    const QString prefix = prefixPortion(text());

    QStringList matches;
    for (const QString& name : m_cachedFolderNames)
    {
        if (name.startsWith(prefix, Qt::CaseInsensitive))
        {
            matches.append(name);
        }
    }

    if (matches.isEmpty())
    {
        hidePopup();
        return;
    }

    m_popup->clear();
    const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    for (const QString& name : matches)
    {
        new QListWidgetItem(folderIcon, name, m_popup);
    }
    m_popup->setCurrentRow(-1);

    const int visibleRows = std::min(static_cast<int>(matches.size()), kMaxVisibleRows);
    const int rowHeight = m_popup->sizeHintForRow(0);
    m_popup->resize(width(), rowHeight * visibleRows + 2 * m_popup->frameWidth());
    m_popup->move(mapToGlobal(QPoint(0, height())));
    m_popup->show();

    applyInlineCompletion(matches, prefix);
}

void AddressBarWidget::hidePopup()
{
    m_popup->hide();
}

void AddressBarWidget::applyInlineCompletion(const QStringList& matches, const QString& typedPrefix)
{
    if (m_lastEditWasDeletion || matches.isEmpty())
    {
        return;
    }

    // Ghost the first matching folder's full name (not just the substring common to every match) --
    // ambiguity between multiple matches is resolved by picking the first one, the same one Tab/Enter
    // would land on if the user just accepted the ghost outright.
    const QString& first = matches.first();
    if (first.length() <= typedPrefix.length())
    {
        return;
    }

    const QString suffix = first.mid(typedPrefix.length());
    const int insertPos = text().length();
    setText(text() + suffix);
    setSelection(insertPos, suffix.length());
}

void AddressBarWidget::acceptCurrentSuggestion()
{
    if (!m_popup->isVisible() || m_popup->currentRow() < 0)
    {
        return;
    }

    const QString name = m_popup->currentItem()->text();
    const std::filesystem::path fullPath = m_cachedDirectory / name.toStdWString();
    setText(QString::fromStdWString(fullPath.wstring()));
    hidePopup();
    emit returnPressed();
}

bool AddressBarWidget::acceptPendingCompletion()
{
    if (m_popup->isVisible() && m_popup->currentRow() >= 0)
    {
        const QString name = m_popup->currentItem()->text();
        const std::filesystem::path fullPath = m_cachedDirectory / name.toStdWString();
        setText(QString::fromStdWString(fullPath.wstring()));
        hidePopup();
        return true;
    }

    if (hasSelectedText())
    {
        // A ghosted inline-completion suffix is already in the text, selected -- commit it in
        // place (deselect) rather than navigating.
        deselect();
        setCursorPosition(text().length());
        hidePopup();
        return true;
    }

    return false;
}

void AddressBarWidget::moveHighlight(int delta)
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

void AddressBarWidget::keyPressEvent(QKeyEvent* event)
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
                acceptCurrentSuggestion();
                event->accept();
                return;
            }
            break;

        default:
            break;
    }

    // Reaching here means the key falls through to normal QLineEdit editing. Backspace/Delete are
    // the only keys that actually remove text without the user having typed a replacement
    // character, so they're the only ones that should suppress re-ghosting on this edit (typing a
    // character over a selected ghost also shrinks the text, but that's a replacement, not a
    // deletion, and must still re-ghost against the new prefix).
    m_lastEditWasDeletion = (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete);

    QLineEdit::keyPressEvent(event);
}

bool AddressBarWidget::event(QEvent* ev)
{
    // A bare Tab is claimed for focus-chain traversal by QWidget::event() *before* keyPressEvent
    // is ever called, so keyPressEvent alone can never intercept it -- it has to be handled here.
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

void AddressBarWidget::focusInEvent(QFocusEvent* event)
{
    QLineEdit::focusInEvent(event);

    // Deferred to the next event-loop iteration: when focus arrives via a mouse click, Qt delivers
    // this focusInEvent *before* the mousePressEvent that placed the cursor/click-selection, so a
    // selectAll() called directly here would just be undone by that mousePressEvent right after.
    QTimer::singleShot(0, this, &QLineEdit::selectAll);
}

void AddressBarWidget::focusOutEvent(QFocusEvent* event)
{
    hidePopup();
    QLineEdit::focusOutEvent(event);
}

std::filesystem::path AddressBarWidget::directoryPortion(const QString& text)
{
    const std::filesystem::path path(text.toStdWString());
    return path.parent_path();
}

QString AddressBarWidget::prefixPortion(const QString& text)
{
    const std::filesystem::path path(text.toStdWString());
    return QString::fromStdWString(path.filename().wstring());
}
