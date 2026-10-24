#pragma once

// One enumerator per hotkey-catalog row (HotkeyCatalogEntry::action, Architecture.md §14.x).
// Append-only across milestones -- new rows add new enumerators at the end, existing ones are
// never reordered/renumbered. The underlying value itself doesn't need to stay stable (persistence
// keys off HotkeyCatalogEntry::stableId, never this enum's value -- see HotkeyBindings), but
// keeping it append-only avoids churning every other milestone's diff for no benefit.
enum class HotkeyAction
{
    // File Operations & Selection (spec §3.6) -- Milestone 1. Every one of these already exists as
    // a hardcoded shortcut in FileBrowserView::handleKeyPress today except SelectAll and
    // Properties, which gain real keyboard access for the first time here.
    Copy,
    Cut,
    Paste,
    Delete,
    DeletePermanently,
    Rename,
    NavigateToParent,
    OpenOrNavigateInto,
    SelectAll,
    Properties,

    // Navigation (spec §3.1) -- Milestone 2. All five are net-new keyboard access; none of these
    // chords collide with any Qt-internal QAbstractItemView default, so handleKeyPress just emits
    // on resolve (no extra swallow-logic needed, unlike SelectAll/OpenOrNavigateInto above).
    NavigateBack,
    NavigateForward,
    Refresh,
    GoToThisPC,
    FocusAddressBar,

    // Tabs (spec §3.2) -- Milestone 3. First window-scoped category: these fire regardless of
    // which widget inside the window currently has focus (HotkeyWindowDispatcher), unlike every
    // enumerator above (all view-scoped, FileBrowserView::handleKeyPress).
    NewTab,
    CloseTab,
    NextTab,
    PreviousTab,

    // Panes & Layout (spec §3.3) -- Milestone 4. Window-scoped, like Tabs above. FocusPaneA-D and
    // CycleNextPane/CyclePreviousPane give real Qt keyboard focus to a pane's current
    // FileBrowserView (WorkspacePaneWidget::focusBrowserView); the Layout* actions reuse
    // MainWindow's existing exclusive m_layoutActionGroup QActions' own WorkspaceController::setLayout
    // call as a second trigger path.
    FocusPaneA,
    FocusPaneB,
    FocusPaneC,
    FocusPaneD,
    CycleNextPane,
    CyclePreviousPane,
    LayoutSingle,
    LayoutTwoVertical,
    LayoutTwoHorizontal,
    LayoutFourGrid,

    // Panels (spec §3.4) -- Milestone 5. Window-scoped, like Tabs/PanesAndLayout above.
    // ToggleLeftPanel/ToggleRightPanel act on MainWindow's long-lived m_leftPanelWidget/
    // m_rightPanelWidget singletons via CollapsiblePanelWidget::toggleExpanded();
    // ToggleBottomPanel resolves the focused pane's current tab's own per-tab BottomPanelWidget
    // (WorkspacePaneWidget::toggleBottomPanel(), no single window-wide bottom-panel widget exists).
    // The five Show*Tab actions are plain CollapsiblePanelWidget::activateTab(index) calls.
    ToggleLeftPanel,
    ToggleRightPanel,
    ToggleBottomPanel,
    ShowLeftTreeTab,
    ShowLeftSearchTab,
    ShowLeftFindInFilesTab,
    ShowRightTagsTab,
    ShowRightPreviewTab,

    // Search & Tools (spec §3.5) -- Milestone 6. Window-scoped, like Tabs/PanesAndLayout/Panels
    // above. Mostly attaching a chord to an already-existing action/entry point
    // (OpenAdvancedSearch/ToggleHiddenFiles/ToggleMediaViewerMode/OpenSettings/OpenTagManager);
    // AddToFavorites reuses FavoritesMenuController's existing "Add to Favorites" QAction
    // (addToFavoritesAction()->trigger()); NewFolder is the one genuinely new entry point here,
    // gaining its own File-menu item alongside the shortcut per spec §5
    // (WorkspacePaneWidget::createNewFolderInCurrentDirectory()). This is the last category to
    // land -- once these 7 rows exist, all 38 spec §3 catalog rows are present.
    OpenAdvancedSearch,
    ToggleHiddenFiles,
    ToggleMediaViewerMode,
    OpenSettings,
    OpenTagManager,
    AddToFavorites,
    NewFolder,
};
