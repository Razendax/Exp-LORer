#pragma once

#include <vector>

#include <QObject>

#include "WorkspacePaneId.h"

class FileNavigationUseCase;
class TagManagementUseCase;
class TabViewModel;
class FileDecorationRules;

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
                            WorkspacePaneId id, QObject* parent = nullptr);

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

signals:
    void tabAdded(int index);
    void tabClosed(int index);
    void activeTabChanged(int index);

private:
    FileNavigationUseCase& m_fileNavigationUseCase;
    TagManagementUseCase& m_tagManagementUseCase;
    const FileDecorationRules& m_fileDecorationRules;
    QObject& m_fileDecorationsChangeSource;
    WorkspacePaneId m_id;
    std::vector<TabViewModel*> m_tabs;
    int m_activeIndex = -1;
};
