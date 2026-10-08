#pragma once

#include <QWidget>

class QLabel;

// Window-wide bottom status bar content (Architecture.md §14.30): a single permanent widget added
// to QMainWindow::statusBar() via addPermanentWidget(..., 1), distinct from the per-pane
// StatusBarWidget (§14.27) and from statusBar()'s own transient showMessage() usage
// (MainWindow::onStatusMessage). Plain display widget, same posture as AddressBarWidget: no
// TabViewModel/use case dependency, just setters + internal layout.
class MainStatusBarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainStatusBarWidget(QWidget* parent = nullptr);

public slots:
    // Empty string blanks the label (no drive to show -- "This PC", an archive-virtual path, or a
    // drive query failure).
    void setDriveInfoText(const QString& text);

private:
    QLabel* m_driveLabel = nullptr;
    QLabel* m_computerNameLabel = nullptr;
};
