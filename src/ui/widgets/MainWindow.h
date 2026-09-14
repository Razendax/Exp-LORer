#pragma once

#include <filesystem>

#include <QMainWindow>

class QTabWidget;
class QLineEdit;
class QAction;
class NavigationViewModel;

// Application shell: menu bar, navigation toolbar (back/forward/up + address bar, bound to
// NavigationViewModel), and a tab area for directory views.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(NavigationViewModel* navigationViewModel, QWidget* parent = nullptr);

private:
    void createMenuBar();
    void createNavigationToolBar();
    void createTabArea();

    void onCurrentPathChanged(const std::filesystem::path& path);
    void onAddressBarEdited();
    void onNavigationFailed(const std::filesystem::path& path, const QString& message);

    NavigationViewModel* m_navigationViewModel = nullptr;

    QTabWidget* m_tabWidget = nullptr;
    QLineEdit* m_addressBar = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_upAction = nullptr;
};
