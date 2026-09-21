#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QEvent;

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

signals:
    // Emitted on every keystroke in the quick-select field (QLineEdit::textChanged).
    void quickSelectTextChanged(const QString& text);

    // Escape was pressed in the quick-select field (already cleared by the time this fires) --
    // the consumer (WorkspacePaneWidget) should return focus to the file list.
    void quickSelectCancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLabel* m_countsLabel = nullptr;
    QLineEdit* m_quickSelectEdit = nullptr;
};
