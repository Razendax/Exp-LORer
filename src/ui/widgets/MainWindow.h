#pragma once

#include <QMainWindow>

class QTabWidget;

// Application shell: menu bar, navigation toolbar, and a tab area for
// directory views. UI chrome only — not yet wired to use cases.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void createMenuBar();
    void createNavigationToolBar();
    void createTabArea();

    QTabWidget* m_tabWidget = nullptr;
};
