#pragma once

#include <filesystem>

#include <QList>
#include <QMainWindow>

#include "ViewMode.h"

class QTabWidget;
class QLineEdit;
class QAction;
class QActionGroup;
class NavigationViewModel;
class FileListModel;
class FileBrowserView;

// Application shell: menu bar, navigation toolbar (back/forward/up + view-mode dropdown +
// address bar, bound to NavigationViewModel), and a tab area holding a FileBrowserView per tab
// (currently a single implicit tab).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(NavigationViewModel* navigationViewModel, QWidget* parent = nullptr);

private:
    void createViewModeActions();
    void createMenuBar();
    void createNavigationToolBar();
    void createTabArea();

    void onCurrentPathChanged(const std::filesystem::path& path);
    void onAddressBarEdited();
    void onNavigationFailed(const std::filesystem::path& path, const QString& message);
    void onViewModeChanged(ViewMode mode);
    void onItemActivated(const std::filesystem::path& path, bool isDirectory);

    NavigationViewModel* m_navigationViewModel = nullptr;
    FileListModel* m_fileListModel = nullptr;
    FileBrowserView* m_fileBrowserView = nullptr;

    QTabWidget* m_tabWidget = nullptr;
    QLineEdit* m_addressBar = nullptr;
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_upAction = nullptr;

    // One QAction per ViewMode (same order as the anonymous-namespace kViewModes array in the
    // .cpp), shared verbatim between the toolbar dropdown and the &View menu so both stay in
    // sync automatically.
    QActionGroup* m_viewModeActionGroup = nullptr;
    QList<QAction*> m_viewModeActions;
};
