#pragma once

#include <QLineEdit>
#include <QStringList>

class QListWidget;
class QEvent;
class QKeyEvent;
class QFocusEvent;

// Drop-in replacement for the tag panel's plain QLineEdit search box (Architecture.md §14.70):
// while typing, shows a popup of matching existing tag names directly under the box, with
// keyboard/mouse navigation and inline "ghost" suffix completion -- the same popup/Tab/ghost
// mechanics as AddressBarWidget (§14.21), mirrored closely, with one deliberate divergence: Enter
// here always acts on the literal typed text (assign-if-exists / create-then-assign) and never
// accepts whatever's highlighted in the popup -- Tab is the only way to pull a suggestion into the
// text box. Has no debounce timer or async suggestion request of its own: the owning
// TagPanelWidget pushes the already-computed candidate list via setSuggestions() on every
// TagListViewModel::searchResultsChanged.
class TagSearchEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit TagSearchEdit(QWidget* parent = nullptr);

public slots:
    // Replaces the popup's candidate list (already ranked exact > prefix > substring, alphabetical
    // within each group, same order as TagListViewModel::searchResults()).
    void setSuggestions(const QStringList& tagNames);

protected:
    // Overridden (rather than just keyPressEvent) because QWidget::event() intercepts a bare Tab
    // press for focus-chain traversal *before* keyPressEvent ever runs -- keyPressEvent alone
    // cannot claim Tab away from that default "move to next widget" behavior.
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void refreshPopup();
    void hidePopup();
    void applyInlineCompletion();
    // Commits whatever completion is pending (a highlighted popup row, an unaccepted ghosted
    // suffix, or -- with neither -- the top-ranked suggestion) into plain, non-ghosted text
    // without assigning it. Returns false if there was nothing to commit, so Tab can fall back to
    // normal focus-chain traversal.
    bool acceptPendingCompletion();
    void moveHighlight(int delta);

    QListWidget* m_popup = nullptr;
    QStringList m_suggestions;

    // Set from the actual key in keyPressEvent (Backspace/Delete), *not* inferred from a text-
    // length comparison -- typing a character over a selected ghost suffix also shrinks the text
    // relative to what was last displayed, which is not a deletion and must still re-ghost.
    bool m_lastEditWasDeletion = false;
};
