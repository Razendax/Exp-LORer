#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;
class QEvent;
class QResizeEvent;

// Per-pane status bar (Architecture.md §14.27): one instance per WorkspacePaneWidget, rebound to
// whichever tab is active the same way the pane's toolbar/address bar already are
// (bindToolBarToTab) -- not one instance per tab page. Plain, ViewModel-agnostic display widget,
// same posture as AddressBarWidget: it only emits signals and lets WorkspacePaneWidget mediate,
// never touches TabViewModel/FileListModel directly.
//
// Distinct from MainWindow's QMainWindow::statusBar(), which stays a transient, window-wide,
// auto-hiding message bar for navigation/search failures (MainWindow::onStatusMessage) -- this bar
// is always visible and pane-local.
class StatusBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBarWidget(QWidget* parent = nullptr);

public slots:
    // "<N> items", or "<N> items, <M> selected" once selectedCount > 0.
    void setCounts(int totalCount, int selectedCount);

    // Clears the quick-select field's text -- the resulting empty-string quickSelectTextChanged
    // is already treated as a no-op by consumers.
    void clearQuickSelect();

    // Git branch indicator (Architecture.md §14.34): hides both the branch button and the
    // changed-count label when !inRepository; otherwise sets the button's text/tooltip to
    // headDisplayName and the label's text to QString::number(changedFileCount).
    void setGitInfo(bool inRepository, const QString& headDisplayName, int changedFileCount);

signals:
    // Emitted on every keystroke in the quick-select field (QLineEdit::textChanged).
    void quickSelectTextChanged(const QString& text);

    // Escape was pressed in the quick-select field (already cleared by the time this fires) --
    // the consumer (WorkspacePaneWidget) should return focus to the file list.
    void quickSelectCancelled();

    // The branch button was clicked -- the consumer (WorkspacePaneWidget) fetches the branch list
    // and calls showBranchMenu in response.
    void branchButtonClicked();

    // A branch was picked from the menu shown by showBranchMenu.
    void branchSelected(const QString& branchName);

public:
    // Builds and pops up a QMenu listing branchNames (one checkable QAction each, checked iff it
    // equals currentBranchName), anchored under the branch button. Each action re-emits
    // branchSelected on trigger.
    void showBranchMenu(const QStringList& branchNames, const QString& currentBranchName);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // Repositions m_gitGroup so it stays horizontally centered on the bar regardless of how wide
    // the counts label or quick-select field currently are -- called from resizeEvent and whenever
    // the group's own content (and therefore its sizeHint) changes in setGitInfo.
    void updateGitGroupGeometry();

    QLabel* m_countsLabel = nullptr;
    QLineEdit* m_quickSelectEdit = nullptr;
    // Deliberately NOT a child item of the main QHBoxLayout: centering it via stretches on both
    // sides only keeps it centered when the left (counts label) and right (quick-select field)
    // widths are equal, which they aren't once the counts label's text changes length. Positioned
    // independently instead so its center is pinned to the bar's midpoint and never shifts.
    QWidget* m_gitGroup = nullptr;
    QToolButton* m_branchButton = nullptr;
    QLabel* m_changedCountLabel = nullptr;
};
