#include "TextEditorFindBar.h"

#include "TextEditorWidget.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QRect>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVector>

TextEditorFindBar::TextEditorFindBar(TextEditorWidget* editor)
    : QWidget(editor)
    , m_editor(editor)
{
    // Keep the bar's own sizeHint compact -- it floats over the editor rather than being
    // stretched by an enclosing layout (see repositionOverEditor).
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    // Floating directly over editor text needs its own opaque background and a border to read
    // as a distinct panel, VS Code-style.
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "TextEditorFindBar { background: palette(window); border: 1px solid palette(mid); border-radius: 4px; }"));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Find"));
    m_searchEdit->setFixedWidth(160);
    m_searchEdit->installEventFilter(this);

    m_statusLabel = new QLabel(this);

    m_previousButton = new QToolButton(this);
    m_previousButton->setText(tr("↑"));
    m_previousButton->setToolTip(tr("Previous match (Shift+Enter)"));

    m_nextButton = new QToolButton(this);
    m_nextButton->setText(tr("↓"));
    m_nextButton->setToolTip(tr("Next match (Enter)"));

    m_caseSensitiveButton = new QToolButton(this);
    m_caseSensitiveButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_caseSensitiveButton->setText(tr("Aa"));
    m_caseSensitiveButton->setCheckable(true);
    m_caseSensitiveButton->setToolTip(tr("Match Case"));

    m_wholeWordButton = new QToolButton(this);
    m_wholeWordButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wholeWordButton->setText(tr("ab"));
    m_wholeWordButton->setCheckable(true);
    m_wholeWordButton->setToolTip(tr("Match Whole Word"));

    m_regexButton = new QToolButton(this);
    m_regexButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_regexButton->setText(tr(".*"));
    m_regexButton->setCheckable(true);
    m_regexButton->setToolTip(tr("Match Regex\nMatches regex pattern"));

    m_closeButton = new QToolButton(this);
    m_closeButton->setText(tr("✕"));
    m_closeButton->setToolTip(tr("Close"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_caseSensitiveButton);
    layout->addWidget(m_wholeWordButton);
    layout->addWidget(m_regexButton);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_previousButton);
    layout->addWidget(m_nextButton);
    layout->addWidget(m_closeButton);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString&) { runSearch(false, true); });
    connect(m_caseSensitiveButton, &QToolButton::toggled, this, [this](bool) { runSearch(false, true); });
    connect(m_wholeWordButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_wholeWordButton, checked); });
    connect(m_regexButton, &QToolButton::toggled, this,
            [this](bool checked) { onMatchModeButtonToggled(m_regexButton, checked); });
    connect(m_nextButton, &QToolButton::clicked, this, [this] { runSearch(false, false); });
    connect(m_previousButton, &QToolButton::clicked, this, [this] { runSearch(true, false); });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        const bool shiftHeld = QApplication::keyboardModifiers() & Qt::ShiftModifier;
        runSearch(shiftHeld, false);
    });
    connect(m_closeButton, &QToolButton::clicked, this, &TextEditorFindBar::hideAndClear);

    // Reposition whenever the editor (this widget's parent) is resized, so the overlay stays
    // anchored to its top-right corner.
    m_editor->installEventFilter(this);
    repositionOverEditor();
}

bool TextEditorFindBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor && event->type() == QEvent::Resize) {
        repositionOverEditor();
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideAndClear();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TextEditorFindBar::showAndFocus()
{
    setVisible(true);
    repositionOverEditor();
    raise();

    const QTextCursor cursor = m_editor->textCursor();
    if (cursor.hasSelection()) {
        const QString selected = cursor.selectedText();
        if (!selected.contains(QChar::ParagraphSeparator))
            m_searchEdit->setText(selected);
    }

    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
    runSearch(false, true);
}

void TextEditorFindBar::repositionOverEditor()
{
    adjustSize();

    const QRect viewportRect = m_editor->viewport()->geometry();
    constexpr int margin = 6;
    const int x = qMax(viewportRect.left(), viewportRect.right() - width() - margin);
    const int y = viewportRect.top() + margin;
    move(x, y);
}

void TextEditorFindBar::hideAndClear()
{
    m_editor->setExtraFindSelections({});
    setVisible(false);
    m_editor->setFocus();
}

void TextEditorFindBar::onMatchModeButtonToggled(QToolButton* button, bool checked)
{
    if (checked) {
        QToolButton* other = (button == m_wholeWordButton) ? m_regexButton : m_wholeWordButton;
        if (other->isChecked()) {
            const QSignalBlocker blocker(other);
            other->setChecked(false);
        }
    }

    runSearch(false, true);
}

void TextEditorFindBar::runSearch(bool backward, bool fromScratch)
{
    const QString term = m_searchEdit->text();
    if (term.isEmpty()) {
        m_editor->setExtraFindSelections({});
        m_statusLabel->clear();
        return;
    }

    const bool useRegex = m_regexButton->isChecked();
    QRegularExpression regex;
    if (useRegex) {
        regex.setPattern(term);
        if (!regex.isValid()) {
            m_editor->setExtraFindSelections({});
            m_statusLabel->setText(tr("Invalid regex"));
            return;
        }
    }

    QTextDocument::FindFlags flags;
    if (m_caseSensitiveButton->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (m_wholeWordButton->isChecked())
        flags |= QTextDocument::FindWholeWords;

    QTextDocument* document = m_editor->document();

    QVector<QTextCursor> matches;
    int from = 0;
    while (true) {
        QTextCursor cursor = useRegex ? document->find(regex, from, flags)
                                       : document->find(term, from, flags);
        if (cursor.isNull())
            break;
        matches.append(cursor);
        from = cursor.selectionEnd();
        if (cursor.selectionStart() == cursor.selectionEnd())
            ++from; // zero-width match (e.g. "a*") -- force progress past it
    }

    if (matches.isEmpty()) {
        m_editor->setExtraFindSelections({});
        m_statusLabel->setText(tr("No results"));
        return;
    }

    QTextCursor anchor = m_editor->textCursor();
    if (backward)
        anchor.setPosition(anchor.selectionStart());

    QTextDocument::FindFlags searchFlags = flags;
    if (backward)
        searchFlags |= QTextDocument::FindBackward;

    QTextCursor result = useRegex ? document->find(regex, anchor, searchFlags)
                                   : document->find(term, anchor, searchFlags);
    if (result.isNull()) {
        // Wrap around: retry once from the document start (forward) or end (backward).
        QTextCursor wrapAnchor(document);
        if (!backward)
            wrapAnchor.movePosition(QTextCursor::Start);
        else
            wrapAnchor.movePosition(QTextCursor::End);
        result = useRegex ? document->find(regex, wrapAnchor, searchFlags)
                           : document->find(term, wrapAnchor, searchFlags);
    }

    int currentIndex = -1;
    if (!result.isNull()) {
        m_editor->setTextCursor(result);
        m_editor->ensureCursorVisible();

        for (int i = 0; i < matches.size(); ++i) {
            if (matches.at(i).selectionStart() == result.selectionStart()
                && matches.at(i).selectionEnd() == result.selectionEnd()) {
                currentIndex = i;
                break;
            }
        }
    }

    QList<QTextEdit::ExtraSelection> selections;
    selections.reserve(matches.size());
    for (const QTextCursor& match : matches) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = match;
        selection.format.setBackground(QColor(255, 255, 0, 120));
        selections.append(selection);
    }
    if (currentIndex >= 0) {
        QTextEdit::ExtraSelection current;
        current.cursor = matches.at(currentIndex);
        current.format.setBackground(m_editor->palette().color(QPalette::Highlight));
        selections.append(current);
    }
    m_editor->setExtraFindSelections(selections);

    if (currentIndex >= 0)
        m_statusLabel->setText(tr("%1/%2").arg(currentIndex + 1).arg(matches.size()));
    else
        m_statusLabel->setText(tr("No results"));
}
