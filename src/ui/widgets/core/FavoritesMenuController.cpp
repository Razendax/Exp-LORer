#include "FavoritesMenuController.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <QAction>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include "DriveSpaceInfoWorker.h"
#include "FavoritesViewModel.h"
#include "FileNavigationUseCase.h"
#include "TabViewModel.h"
#include "VirtualPaths.h"
#include "WorkspaceController.h"
#include "WorkspacePaneWidget.h"

namespace
{
    // Same QFileIconProvider-based lookup FileListModel/MonitoringLogModel use for file/folder
    // icons, applied to a favorite's target path.
    QIcon favoriteItemIcon(const std::filesystem::path& path)
    {
        static QFileIconProvider provider;
        return provider.icon(QFileInfo(QString::fromStdWString(path.wstring())));
    }

    QIcon favoriteCategoryIcon()
    {
        static QFileIconProvider provider;
        return provider.icon(QFileIconProvider::Folder);
    }

    // Custom QWidgetAction row for a favorite item: icon + name + a second, smaller/grayed line
    // with the full path, all as ONE menu entry rather than two separate actions. Both this widget
    // and its child labels are WA_TransparentForMouseEvents so mouse clicks/moves fall through to
    // the owning QMenu untouched -- QMenu then hit-tests, highlights, triggers and closes the
    // cascade exactly as it would for a plain QAction; only the highlight repaint (menu doesn't
    // paint anything behind a widget-action row) and the highlighted-text color swap are done here,
    // driven by the menu's hovered(QAction*) signal (see rebuildMenuItems below).
    class FavoriteMenuRowWidget : public QWidget
    {
    public:
        FavoriteMenuRowWidget(const QIcon& icon, const QString& name, const QString& path, QWidget* parent = nullptr)
            : QWidget(parent)
        {
            setAttribute(Qt::WA_TransparentForMouseEvents);

            auto* layout = new QHBoxLayout(this);
            layout->setContentsMargins(4, 3, 16, 3);
            layout->setSpacing(6);

            auto* iconLabel = new QLabel(this);
            iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            iconLabel->setPixmap(icon.pixmap(16, 16));
            layout->addWidget(iconLabel);

            auto* textLayout = new QVBoxLayout();
            textLayout->setContentsMargins(0, 0, 0, 0);
            textLayout->setSpacing(0);

            m_nameLabel = new QLabel(name, this);
            m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            textLayout->addWidget(m_nameLabel);

            m_pathLabel = new QLabel(path, this);
            m_pathLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            QFont pathFont = m_pathLabel->font();
            pathFont.setPointSizeF(pathFont.pointSizeF() * 0.85);
            m_pathLabel->setFont(pathFont);
            QPalette pathPalette = m_pathLabel->palette();
            pathPalette.setColor(QPalette::WindowText, Qt::gray);
            m_pathLabel->setPalette(pathPalette);
            textLayout->addWidget(m_pathLabel);

            layout->addLayout(textLayout, 1);

            m_normalNamePalette = m_nameLabel->palette();
            m_normalPathPalette = m_pathLabel->palette();
        }

        void setHighlighted(bool highlighted)
        {
            if (m_highlighted == highlighted)
            {
                return;
            }
            m_highlighted = highlighted;

            if (!highlighted)
            {
                m_nameLabel->setPalette(m_normalNamePalette);
                m_pathLabel->setPalette(m_normalPathPalette);
                update();
                return;
            }

            QPalette namePalette = m_normalNamePalette;
            namePalette.setColor(QPalette::WindowText, namePalette.color(QPalette::HighlightedText));
            m_nameLabel->setPalette(namePalette);

            QPalette pathPalette = m_normalPathPalette;
            pathPalette.setColor(QPalette::WindowText, pathPalette.color(QPalette::HighlightedText));
            m_pathLabel->setPalette(pathPalette);
            update();
        }

    protected:
        // QMenu paints nothing behind a widget-action row, so the usual blue selection band has to
        // be drawn here -- otherwise hovering only swaps the text to (invisible-on-white)
        // highlighted-text color, per the bug this fixed.
        void paintEvent(QPaintEvent* event) override
        {
            if (m_highlighted)
            {
                QPainter painter(this);
                painter.fillRect(rect(), palette().color(QPalette::Highlight));
            }
            QWidget::paintEvent(event);
        }

    private:
        QLabel* m_nameLabel = nullptr;
        QLabel* m_pathLabel = nullptr;
        QPalette m_normalNamePalette;
        QPalette m_normalPathPalette;
        bool m_highlighted = false;
    };
}

FavoritesMenuController::FavoritesMenuController(FavoritesViewModel& favoritesViewModel, FileNavigationUseCase& fileNavigationUseCase,
                                                   WorkspaceController& workspaceController,
                                                   std::function<WorkspacePaneWidget*(WorkspacePaneId)> paneWidgetResolver,
                                                   QWidget* parent)
    : QObject(parent)
    , m_favoritesViewModel(favoritesViewModel)
    , m_workspaceController(workspaceController)
    , m_paneWidgetResolver(std::move(paneWidgetResolver))
{
    m_statWorker = new DriveSpaceInfoWorker(fileNavigationUseCase);
    m_statWorker->moveToThread(&m_statThread);
    m_statThread.start();

    m_menu = new QMenu(tr("F&avorites"), parent);

    m_addToFavoritesAction = new QAction(tr("Add to Favorites"), this);
    connect(m_addToFavoritesAction, &QAction::triggered, this, &FavoritesMenuController::onAddToFavoritesTriggered);
    m_menu->addAction(m_addToFavoritesAction);

    m_menu->addAction(tr("Manage Favorites..."), this, [this]() { emit manageFavoritesRequested(); });
    m_menu->addSeparator();

    // Rebuilt lazily on aboutToShow rather than kept live-synced against
    // FavoritesViewModel::favoritesChanged() -- always current with no extra wiring here
    // (Architecture.md §14.35).
    connect(m_menu, &QMenu::aboutToShow, this, &FavoritesMenuController::rebuildMenuItems);
}

FavoritesMenuController::~FavoritesMenuController()
{
    m_statThread.quit();
    m_statThread.wait();
    delete m_statWorker;
}

void FavoritesMenuController::setFocusedTab(TabViewModel* tab)
{
    if (m_trackedTab)
    {
        disconnect(m_trackedTab, &TabViewModel::selectedEntriesChanged, this, &FavoritesMenuController::updateActionEnabled);
        disconnect(m_trackedTab, &TabViewModel::currentPathChanged, this, &FavoritesMenuController::updateActionEnabled);
    }
    m_trackedTab = tab;

    if (m_trackedTab)
    {
        connect(m_trackedTab, &TabViewModel::selectedEntriesChanged, this, &FavoritesMenuController::updateActionEnabled);
        connect(m_trackedTab, &TabViewModel::currentPathChanged, this, &FavoritesMenuController::updateActionEnabled);
    }

    updateActionEnabled();
}

void FavoritesMenuController::onAddToFavoritesTriggered()
{
    TabViewModel* tab = m_workspaceController.focusedTab();
    if (!tab)
    {
        return;
    }

    const std::vector<FileNode>& selected = tab->selectedEntries();
    if (selected.empty())
    {
        // Falls back to the focused tab's current browsed folder itself, mirroring the Tag
        // Panel's "no selection" convention (Architecture.md §14.9/§14.35).
        if (tab->currentPath() != VirtualPaths::ThisPC)
        {
            m_favoritesViewModel.addItem(tab->currentPath());
        }
        return;
    }

    // Every selected entry becomes its own top-level, uncategorized favorite (Architecture.md
    // §14.35) -- categorization only happens from the Manage Favorites dialog.
    for (const FileNode& entry : selected)
    {
        m_favoritesViewModel.addItem(entry.path());
    }
}

void FavoritesMenuController::onFavoriteItemTriggered(Favorites::Id id)
{
    std::optional<std::filesystem::path> targetPath;
    for (const Favorites::Entry& entry : m_favoritesViewModel.entries())
    {
        if (const auto* item = std::get_if<FavoriteItem>(&entry); item && item->id() == id)
        {
            targetPath = item->path();
            break;
        }
        if (const auto* category = std::get_if<FavoriteCategory>(&entry))
        {
            const auto& items = category->items();
            const auto it = std::find_if(items.begin(), items.end(), [id](const FavoriteItem& item) { return item.id() == id; });
            if (it != items.end())
            {
                targetPath = it->path();
                break;
            }
        }
    }

    if (!targetPath)
    {
        return;
    }

    WorkspacePaneWidget* paneWidget = m_paneWidgetResolver(m_workspaceController.focusedPane());
    if (!paneWidget)
    {
        return;
    }

    const std::filesystem::path path = *targetPath;
    QPointer<WorkspacePaneWidget> paneGuard(paneWidget);

    // FileNavigationUseCase::stat() is disk I/O and must never run on the Qt UI thread -- runs on
    // this controller's own dedicated m_statWorker/m_statThread (Architecture.md §14.96) rather
    // than reaching into MainWindow's drive-info worker for an unrelated one-off call. Both hops
    // are queued functor calls, the same shape MainWindow::onActiveTabPathChanged/
    // onDriveSpaceInfoReady already use.
    DriveSpaceInfoWorker* statWorker = m_statWorker;
    QMetaObject::invokeMethod(
        statWorker,
        [this, statWorker, paneGuard, path]() {
            Result<FileNode> stat = statWorker->stat(path);

            QMetaObject::invokeMethod(
                this,
                [this, paneGuard, path, stat = std::move(stat)]() mutable {
                    if (!paneGuard)
                    {
                        // The focused pane changed/closed while the stat was in flight.
                        return;
                    }

                    if (!stat)
                    {
                        // Target no longer exists on disk -- surfaces through the same status-bar
                        // path as any other bad navigation target (Architecture.md §14.35), no modal.
                        emit statusMessage(QString::fromStdString(stat.error().message));
                        return;
                    }

                    if (stat.value().isDirectory())
                    {
                        // Folder favorite: navigate into it (Explorer Quick-Access-style).
                        paneGuard->navigateAndSelect(path, std::nullopt);
                    }
                    else
                    {
                        // File favorite: navigate to its parent folder with the file selected.
                        paneGuard->navigateAndSelect(path.parent_path(), path);
                    }
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void FavoritesMenuController::rebuildMenuItems()
{
    // The first 3 actions (Add to Favorites, Manage Favorites..., separator) are static, added
    // once by the constructor -- only entries after them are torn down and rebuilt here.
    constexpr int kStaticActionCount = 3;

    const QList<QAction*> actions = m_menu->actions();
    for (int i = actions.size() - 1; i >= kStaticActionCount; --i)
    {
        QAction* action = actions[i];
        m_menu->removeAction(action);
        if (QMenu* submenu = action->menu())
        {
            // Deleting the submenu also deletes its owned menuAction() (the action itself).
            submenu->deleteLater();
        }
        else
        {
            action->deleteLater();
        }
    }

    // Adds ONE menu entry per favorite item -- a QWidgetAction whose FavoriteMenuRowWidget renders
    // the name and, below it, the smaller/grayed full path -- rather than a separate action per
    // line. menu's hovered() signal drives the row's highlight repaint since QMenu itself paints
    // nothing behind a widget-action row (see FavoriteMenuRowWidget's comment above).
    auto addFavoriteItemAction = [this](QMenu* menu, const FavoriteItem& favoriteItem)
    {
        const Favorites::Id id = favoriteItem.id();
        const QString name = QString::fromStdWString(favoriteItem.displayName().wstring());
        const QString path = QString::fromStdWString(favoriteItem.path().wstring());

        auto* rowWidget = new FavoriteMenuRowWidget(favoriteItemIcon(favoriteItem.path()), name, path);

        auto* action = new QWidgetAction(menu);
        action->setToolTip(path);
        action->setDefaultWidget(rowWidget);
        connect(action, &QAction::triggered, this, [this, id]() { onFavoriteItemTriggered(id); });
        menu->addAction(action);

        connect(menu, &QMenu::hovered, rowWidget, [rowWidget, action](QAction* hoveredAction) {
            rowWidget->setHighlighted(hoveredAction == action);
        });
    };

    for (const Favorites::Entry& entry : m_favoritesViewModel.entries())
    {
        if (const auto* item = std::get_if<FavoriteItem>(&entry))
        {
            addFavoriteItemAction(m_menu, *item);
        }
        else if (const auto* category = std::get_if<FavoriteCategory>(&entry))
        {
            QMenu* submenu = m_menu->addMenu(favoriteCategoryIcon(), QString::fromStdString(category->name()));
            for (const FavoriteItem& categoryItem : category->items())
            {
                addFavoriteItemAction(submenu, categoryItem);
            }
        }
    }
}

void FavoritesMenuController::updateActionEnabled()
{
    const bool enabled = m_trackedTab && m_trackedTab->currentPath() != VirtualPaths::ThisPC;
    m_addToFavoritesAction->setEnabled(enabled);
}
