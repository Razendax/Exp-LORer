#pragma once

#include <QKeySequence>
#include <QPushButton>

// One row's clickable "current shortcut" cell in SettingsHotkeysPage (spec §4): shows the chord's
// display text; clicking enters "listening" mode (distinct stylesheet state + placeholder text),
// and the next QKeyEvent it receives becomes the candidate chord -- Escape alone cancels back to
// the prior display without changing anything. A candidate with no modifier is rejected (stays in
// listening mode) unless allowsBareRebind is set. Acceptance emits chordAccepted() and exits
// listening mode; the actual HotkeyManager::setBinding call (and re-reading the chord that stuck,
// in case HotkeyManager itself rejects it for some other reason) is left to the owning page.
class HotkeyEditWidget : public QPushButton
{
    Q_OBJECT

public:
    explicit HotkeyEditWidget(QWidget* parent = nullptr);

    void setChord(const QKeySequence& chord);
    void setAllowsBareRebind(bool allowsBareRebind) noexcept { m_allowsBareRebind = allowsBareRebind; }

signals:
    void chordAccepted(const QKeySequence& chord);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void enterListeningMode();
    void exitListeningMode();
    void refreshText();

    QKeySequence m_chord;
    bool m_allowsBareRebind = false;
    bool m_listening = false;
};
