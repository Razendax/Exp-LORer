#include "TextEditorFindBar.h"

#include "TextEditorWidget.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
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

namespace {

// Translates VS Code/JS-style "$1", "$2", ... capture-group references (and "$$" as a literal
// "$") into Qt's own "\1", "\2", ... backreference syntax expected by
// QString::replace(QRegularExpression, QString). Done as a manual character scan rather than a
// nested regex substitution to avoid double-escaping bugs. Any literal backslash the user typed
// is doubled first so it survives as a literal backslash rather than being misread as a Qt
// backreference once translated.
QString translateDollarCaptureRefs(const QString& replacement)
{
    QString result;
    result.reserve(replacement.size());

    for (int i = 0; i < replacement.size(); ++i) {
        const QChar ch = replacement.at(i);
        if (ch == QLatin1Char('\\')) {
            result += QStringLiteral("\\\\");
        } else if (ch == QLatin1Char('$')) {
            if (i + 1 < replacement.size() && replacement.at(i + 1) == QLatin1Char('$')) {
                result += QLatin1Char('$');
                ++i;
            } else {
                int digitsEnd = i + 1;
                while (digitsEnd < replacement.size() && replacement.at(digitsEnd).isDigit())
                    ++digitsEnd;
                if (digitsEnd > i + 1) {
                    result += QLatin1Char('\\');
                    result += replacement.mid(i + 1, digitsEnd - i - 1);
                    i = digitsEnd - 1;
                } else {
                    result += ch;
                }
            }
        } else {
            result += ch;
        }
    }

    return result;
}

} // namespace

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
    m_searchEdit->setFixedWidth(200);
    m_searchEdit->installEventFilter(this);

    // Fixed width sized from the widest expected text (translated "No results", or a plausible
    // match count) so the bar's overall width -- and so the position of the Previous/Next/Close
    // buttons to its right -- never shifts as the count/text changes.
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    {
        const QFontMetrics fm(m_statusLabel->font());
        const int width = qMax(fm.horizontalAdvance(tr("No results")),
                                fm.horizontalAdvance(QStringLiteral("9999/9999")));
        m_statusLabel->setFixedWidth(width + 6);
    }

    m_previousButton = new QToolButton(this);
    m_previousButton->setText(tr("↑"));
    m_previousButton->setToolTip(tr("Previous match (Shift+Enter)"));

    m_nextButton = new QToolButton(this);
    m_nextButton->setText(tr("↓"));
    m_nextButton->setToolTip(tr("Next match (Enter)"));

    // These three live inside m_searchEdit itself (see positionSearchEditOverlayButtons), floating
    // over its right edge VS Code-style, rather than taking their own layout column next to it.
    m_caseSensitiveButton = new QToolButton(m_searchEdit);
    m_caseSensitiveButton->setAutoRaise(true);
    m_caseSensitiveButton->setFocusPolicy(Qt::NoFocus);
    m_caseSensitiveButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_caseSensitiveButton->setText(tr("Aa"));
    m_caseSensitiveButton->setCheckable(true);
    m_caseSensitiveButton->setToolTip(tr("Match Case"));

    m_wholeWordButton = new QToolButton(m_searchEdit);
    m_wholeWordButton->setAutoRaise(true);
    m_wholeWordButton->setFocusPolicy(Qt::NoFocus);
    m_wholeWordButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_wholeWordButton->setText(tr("ab"));
    m_wholeWordButton->setCheckable(true);
    m_wholeWordButton->setToolTip(tr("Match Whole Word"));

    m_regexButton = new QToolButton(m_searchEdit);
    m_regexButton->setAutoRaise(true);
    m_regexButton->setFocusPolicy(Qt::NoFocus);
    m_regexButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_regexButton->setText(tr(".*"));
    m_regexButton->setCheckable(true);
    m_regexButton->setToolTip(tr("Match Regex\nMatches regex pattern"));

    m_closeButton = new QToolButton(this);
    m_closeButton->setText(tr("✕"));
    m_closeButton->setToolTip(tr("Close"));

    m_toggleReplaceButton = new QToolButton(this);
    m_toggleReplaceButton->setText(tr("▸"));
    m_toggleReplaceButton->setCheckable(true);
    m_toggleReplaceButton->setToolTip(tr("Show Replace"));

    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setPlaceholderText(tr("Replace"));
    m_replaceEdit->setFixedWidth(200);
    m_replaceEdit->installEventFilter(this);
    m_replaceEdit->setVisible(false);

    m_replaceButton = new QToolButton(this);
    m_replaceButton->setText(tr("Replace"));
    m_replaceButton->setToolTip(tr("Replace (Enter)"));
    m_replaceButton->setVisible(false);

    m_replaceAllButton = new QToolButton(this);
    m_replaceAllButton->setText(tr("Replace All"));
    m_replaceAllButton->setToolTip(tr("Replace All (Ctrl+Alt+Enter)"));
    m_replaceAllButton->setVisible(false);

    // A single QGridLayout (rather than two independent per-row QHBoxLayouts) so column 0 (the
    // toggle button) and column 1 (the search/replace edits) are always sized and positioned
    // identically between the two rows by the layout engine itself -- no manual width-mirroring
    // needed, and it keeps working even if m_toggleReplaceButton's glyph (and so its sizeHint)
    // changes. The remaining per-row buttons live in their own small QHBoxLayout per row (column
    // 2) so they don't force unrelated columns to widen to fit "Replace All". The Aa/ab/.* mode
    // buttons aren't in here at all -- they float inside m_searchEdit itself, positioned by
    // positionSearchEditOverlayButtons.
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(4, 2, 4, 2);
    grid->setHorizontalSpacing(2);
    grid->setVerticalSpacing(2);

    grid->addWidget(m_toggleReplaceButton, 0, 0);
    grid->addWidget(m_searchEdit, 0, 1);

    auto* findExtrasLayout = new QHBoxLayout();
    findExtrasLayout->setContentsMargins(0, 0, 0, 0);
    findExtrasLayout->setSpacing(2);
    findExtrasLayout->addWidget(m_statusLabel);
    findExtrasLayout->addWidget(m_previousButton);
    findExtrasLayout->addWidget(m_nextButton);
    findExtrasLayout->addWidget(m_closeButton);
    grid->addLayout(findExtrasLayout, 0, 2);

    grid->addWidget(m_replaceEdit, 1, 1);

    auto* replaceExtrasLayout = new QHBoxLayout();
    replaceExtrasLayout->setContentsMargins(0, 0, 0, 0);
    replaceExtrasLayout->setSpacing(2);
    replaceExtrasLayout->addWidget(m_replaceButton);
    replaceExtrasLayout->addWidget(m_replaceAllButton);
    grid->addLayout(replaceExtrasLayout, 1, 2);

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
    connect(m_toggleReplaceButton, &QToolButton::toggled, this, &TextEditorFindBar::onToggleReplaceRow);
    connect(m_replaceButton, &QToolButton::clicked, this, &TextEditorFindBar::onReplace);
    connect(m_replaceAllButton, &QToolButton::clicked, this, &TextEditorFindBar::onReplaceAll);

    // Reposition whenever the editor (this widget's parent) is resized, so the overlay stays
    // anchored to its top-right corner.
    m_editor->installEventFilter(this);
    repositionOverEditor();
    positionSearchEditOverlayButtons();
}

bool TextEditorFindBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_editor && event->type() == QEvent::Resize) {
        repositionOverEditor();
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_searchEdit && event->type() == QEvent::Resize) {
        positionSearchEditOverlayButtons();
        return QWidget::eventFilter(watched, event);
    }
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideAndClear();
            return true;
        }
    }
    if (watched == m_replaceEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideAndClear();
            return true;
        }
        // Intercepted here rather than via QLineEdit::returnPressed, same reasoning as
        // TextEditorGotoLineBar::eventFilter: that signal lets Return bubble up afterward and
        // land on the editor as a newline.
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            const bool ctrlAltHeld = (keyEvent->modifiers() & Qt::ControlModifier)
                && (keyEvent->modifiers() & Qt::AltModifier);
            if (ctrlAltHeld)
                onReplaceAll();
            else
                onReplace();
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

void TextEditorFindBar::positionSearchEditOverlayButtons()
{
    constexpr int rightPadding = 4;
    constexpr int buttonSpacing = 2;
    constexpr int buttonVerticalMargin = 3;

    const int buttonHeight = qMax(16, m_searchEdit->height() - 2 * buttonVerticalMargin);
    const int y = (m_searchEdit->height() - buttonHeight) / 2;

    // Rightmost-first: regex closest to the field's edge, then whole word, then match case --
    // matching the left-to-right "Aa ab .*" reading order in the field.
    QToolButton* buttons[] = { m_regexButton, m_wholeWordButton, m_caseSensitiveButton };
    int x = m_searchEdit->width() - rightPadding;
    int leftmostButtonX = x;
    for (QToolButton* button : buttons) {
        const int buttonWidth = qMax(buttonHeight, button->sizeHint().width());
        x -= buttonWidth;
        button->setGeometry(x, y, buttonWidth, buttonHeight);
        leftmostButtonX = x;
        x -= buttonSpacing;
    }

    // Push typed text/cursor out from under the overlaid buttons: the gap from the field's right
    // edge back to the leftmost button, plus a little breathing room before the text.
    const int reservedWidth = m_searchEdit->width() - leftmostButtonX + buttonSpacing;
    m_searchEdit->setTextMargins(0, 0, reservedWidth, 0);
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

void TextEditorFindBar::onToggleReplaceRow(bool checked)
{
    m_toggleReplaceButton->setText(checked ? tr("▾") : tr("▸"));
    m_toggleReplaceButton->setToolTip(checked ? tr("Hide Replace") : tr("Show Replace"));
    m_replaceEdit->setVisible(checked);
    m_replaceButton->setVisible(checked);
    m_replaceAllButton->setVisible(checked);
    repositionOverEditor();
    if (checked) {
        m_replaceEdit->setFocus();
        m_replaceEdit->selectAll();
    }
}

QString TextEditorFindBar::computeReplacement(const QTextCursor& match) const
{
    if (!m_regexButton->isChecked())
        return m_replaceEdit->text();

    QString matchedText = match.selectedText();
    const QRegularExpression regex(m_searchEdit->text());
    matchedText.replace(regex, translateDollarCaptureRefs(m_replaceEdit->text()));
    return matchedText;
}

void TextEditorFindBar::onReplace()
{
    if (m_searchEdit->text().isEmpty())
        return;

    if (m_currentMatch.isNull()) {
        runSearch(false, false);
        return;
    }

    QTextCursor match = m_currentMatch;
    const QString replacement = computeReplacement(match);
    match.insertText(replacement);
    m_editor->setTextCursor(match);
    runSearch(false, true);
}

void TextEditorFindBar::onReplaceAll()
{
    if (m_matches.isEmpty())
        return;

    QTextCursor primary = m_editor->textCursor();
    primary.beginEditBlock();
    for (QTextCursor& match : m_matches) {
        const QString replacement = computeReplacement(match);
        match.insertText(replacement);
    }
    primary.endEditBlock();

    runSearch(false, true);
}

void TextEditorFindBar::runSearch(bool backward, bool fromScratch)
{
    const QString term = m_searchEdit->text();
    if (term.isEmpty()) {
        m_editor->setExtraFindSelections({});
        m_statusLabel->clear();
        m_matches.clear();
        m_currentMatch = QTextCursor();
        return;
    }

    const bool useRegex = m_regexButton->isChecked();
    QRegularExpression regex;
    if (useRegex) {
        regex.setPattern(term);
        if (!regex.isValid()) {
            m_editor->setExtraFindSelections({});
            m_statusLabel->setText(tr("Invalid regex"));
            m_matches.clear();
            m_currentMatch = QTextCursor();
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
        m_matches.clear();
        m_currentMatch = QTextCursor();
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

    m_matches = matches;
    m_currentMatch = currentIndex >= 0 ? matches.at(currentIndex) : QTextCursor();

    if (currentIndex >= 0)
        m_statusLabel->setText(tr("%1/%2").arg(currentIndex + 1).arg(matches.size()));
    else
        m_statusLabel->setText(tr("No results"));
}
