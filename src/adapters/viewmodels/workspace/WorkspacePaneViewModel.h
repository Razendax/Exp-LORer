#pragma once

#include <vector>

#include <QObject>

#include "WorkspacePaneId.h"

class FileNavigationUseCase;
class TagManagementUseCase;
class TabViewModel;
class FileDecorationRules;
class GitStatusService;
class FolderMonitorUseCase;
class AdvancedSearchService;
class ContentSearchService;
class FileTagsService;
class DiskUsageService;
class FragmentationService;
class IconLoadingService;
class MediaThumbnailLoadingService;
class TagChangeNotifier;
class NotificationCenter;

// Per-pane tab bookkeeping: owns an ordered list of TabViewModel children and tracks which one is
// active. One instance per WorkspacePaneId, always alive for the pane's lifetime regardless of
// whether the pane is currently visible under the active SplitLayout (Architecture.md §14).
class WorkspacePaneViewModel : public QObject
{
    Q_OBJECT

public:
    // See TabViewModel's constructor comment for why fileDecorationsChangeSource is a bare QObject&.
    WorkspacePaneViewModel(FileNavigationUseCase& fileNavigationUseCase, TagManagementUseCase& tagManagementUseCase,
                            const FileDecorationRules& fileDecorationRules, QObject& fileDecorationsChangeSource,
                            GitStatusService& gitStatusService, FolderMonitorUseCase& folderMonitorUseCase,
                            AdvancedSearchService& advancedSearchService, ContentSearchService& contentSearchService,
                            FileTagsService& fileTagsService, DiskUsageService& diskUsageService,
                            FragmentationService& fragmentationService,
                            IconLoadingService& iconLoadingService, MediaThumbnailLoadingService& mediaThumbnailLoadingService,
                            TagChangeNotifier& tagChangeNotifier, NotificationCenter& notificationCenter, WorkspacePaneId id,
                            QObject* parent = nullptr);

    WorkspacePaneId id() const noexcept { return m_id; }
    int tabCount() const noexcept { return static_cast<int>(m_tabs.size()); }
    TabViewModel* tabAt(int index) const;
    TabViewModel* activeTab() const;
    int activeIndex() const noexcept { return m_activeIndex; }

public slots:
    // Creates a new TabViewModel, appends it, and makes it the active tab. Emits tabAdded(index)
    // then activeTabChanged(index).
    TabViewModel* addTab();

    // No-op when tabCount() == 1 (mirrors the existing "disable Close Tab when one remains"
    // policy, now per-pane). Otherwise removes the tab at index and adjusts the active index,
    // emitting tabClosed(index) then activeTabChanged(index) if the active tab changed.
    void closeTab(int index);

    void setActiveTab(int index);

    // Reorders the tab at `from` to `to` (both must be valid indices; a no-op if they're equal).
    // Keeps m_activeIndex pointing at the same TabViewModel* it did before the move, then emits
    // tabMoved(from, to). Mirrors the tab bar's own QTabBar::tabMoved reordering so the two stay
    // in sync -- callers must not re-touch the tab bar itself from this slot.
    void moveTab(int from, int to);

    // Cross-pane move (spec §3, Milestone M2): removes the tab at `index` from this pane's model
    // *without* destroying it -- unlike closeTab(), does not stop monitoring and does not
    // deleteLater() it, since the whole point is that it survives unchanged into another pane.
    // Same <= 1-tab no-op guard as closeTab(); reuses closeTab()'s exact active-index-adjustment
    // math (tab before the removed one becomes active; if the removed tab was first, the new-first
    // tab becomes active). Returns nullptr if index is invalid or this pane has only 1 tab.
    TabViewModel* takeTab(int index);

    // Cross-pane move counterpart of takeTab(): reparents `tab` to this pane (QObject::setParent),
    // inserts it into this pane's model at `index` (clamped to [0, tabCount()]), and makes it the
    // active tab -- every spec call site wants the moved tab active in its destination. Emits
    // tabInserted(index) then the resulting activeTabChanged.
    void insertTab(TabViewModel* tab, int index);

signals:
    void tabAdded(int index);
    void tabClosed(int index);
    void activeTabChanged(int index);
    void tabMoved(int from, int to);

    // Cross-pane move (spec §3, Milestone M2): deliberately distinct from tabClosed/tabAdded --
    // WorkspacePaneWidget must not delete the page on tabRemoved, nor build a new one on
    // tabInserted; it already has the page in hand via detachTabPage()/attachTabPage().
    void tabRemoved(int index);
    void tabInserted(int index);

private:
    FileNavigationUseCase& m_fileNavigationUseCase;
    TagManagementUseCase& m_tagManagementUseCase;
    const FileDecorationRules& m_fileDecorationRules;
    QObject& m_fileDecorationsChangeSource;
    GitStatusService& m_gitStatusService;
    FolderMonitorUseCase& m_folderMonitorUseCase;
    AdvancedSearchService& m_advancedSearchService;
    ContentSearchService& m_contentSearchService;
    FileTagsService& m_fileTagsService;
    DiskUsageService& m_diskUsageService;
    FragmentationService& m_fragmentationService;
    IconLoadingService& m_iconLoadingService;
    MediaThumbnailLoadingService& m_mediaThumbnailLoadingService;
    TagChangeNotifier& m_tagChangeNotifier;
    NotificationCenter& m_notificationCenter;
    WorkspacePaneId m_id;
    std::vector<TabViewModel*> m_tabs;
    int m_activeIndex = -1;
};
