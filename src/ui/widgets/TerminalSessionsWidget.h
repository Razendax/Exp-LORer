#pragma once

#include <vector>

#include <QWidget>

#include "TerminalShellType.h"

class QBoxLayout;
class QEvent;
class QLabel;
class QSplitter;
class QStackedWidget;
class QToolButton;
class TerminalWidget;
class UiThemeChangeNotifier;

// Terminal panel-tab content (Architecture.md §14.24): a QStackedWidget of concurrent
// TerminalWidget sessions (left) plus a right-side column -- a '+' button to pick a shell (Command
// Prompt / PowerShell) and a list of open sessions to switch between or close -- replacing the
// single always-cmd TerminalWidget that used to be the panel tab's content directly. The two sit in
// a QSplitter (same drag-to-resize mechanics as MainWindow's left/right SidePanelWidget splitter) so
// the column's width is user-adjustable, and it's also collapsible: a toggle button (always visible,
// even collapsed) shrinks it down to just that button so the terminal can have the full width,
// independent of the session count. Collapsing remembers the width it was dragged to and restores it
// on expand.
// Each row is a plain flush QWidget (full-width flat background, no border-radius/padding "pill"
// shape) rather than a QToolButton, so the column reads as an actual list; a literal QListWidget was
// tried and reverted because QListWidget::setItemWidget() swallows UIA's per-row exposure, so the
// row/label/close-button automation ids (Architecture.md §11) never show up in the accessibility
// tree once wrapped in a list item.
// Starting every session (including the first) still happens outside this widget: sessionCreated()
// hands the new TerminalWidget to the owner (WorkspacePaneWidget), which knows the tab's working
// directory and calls start() on it.
class TerminalSessionsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TerminalSessionsWidget(UiThemeChangeNotifier& uiThemeChangeNotifier, QWidget* parent = nullptr);

    // Creates the first session (cmd) if none exist yet; no-op otherwise. Called by the owner on
    // the Terminal panel tab's first activation, mirroring the old single-TerminalWidget's lazy
    // start.
    void activateDefaultSession();

signals:
    // Emitted right after a new session's TerminalWidget is constructed and made active, for both
    // the default session and every '+' menu selection -- the owner calls start(cwd) on it (and
    // wires restartRequested() for later restarts) since only the owner knows the tab's working
    // directory.
    void sessionCreated(TerminalWidget* widget);

protected:
    // Each row installs this filter on itself (its label is WA_TransparentForMouseEvents, so clicks
    // and hover over the label area reach the row instead) so a click anywhere on the row other than
    // its close button selects that session.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Session
    {
        int id = 0;
        TerminalShellType type = TerminalShellType::Cmd;
        TerminalWidget* terminal = nullptr;
        QWidget* row = nullptr;
        QToolButton* closeButton = nullptr;
    };

    void addSession(TerminalShellType shellType);
    void closeSession(int id);
    void setActiveSession(int id);
    QString nextSessionLabel(TerminalShellType shellType);

    // Re-applies the right-side column's and session rows' UiColors::current()-based stylesheets
    // (Architecture.md §14.50).
    void applyThemeColors();

    // Toggles the row's "selected" dynamic property and forces Qt to re-evaluate its stylesheet
    // (dynamic property selectors aren't picked up automatically -- see applyThemeColors()).
    void applyRowSelectedStyle(const Session& session, bool selected);

    void setSessionsPanelExpanded(bool expanded);

    QSplitter* m_splitter = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_emptyStatePage = nullptr;
    QWidget* m_sideColumn = nullptr;
    QToolButton* m_collapseButton = nullptr;
    QWidget* m_sideContent = nullptr;
    QToolButton* m_addSessionButton = nullptr;
    QBoxLayout* m_sessionListLayout = nullptr;

    std::vector<Session> m_sessions;
    int m_nextSessionId = 1;
    int m_activeSessionId = -1;

    int m_cmdSessionCount = 0;
    int m_powerShellSessionCount = 0;

    bool m_sessionsPanelExpanded = true;
    // The column's width while expanded, updated live as the user drags the splitter handle (see the
    // splitterMoved connection in the constructor) so collapsing and re-expanding restores it rather
    // than snapping back to the original default.
    int m_preferredExpandedWidth = 0;
};
