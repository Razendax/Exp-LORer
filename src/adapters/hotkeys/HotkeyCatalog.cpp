#include "HotkeyCatalog.h"

#include <QCoreApplication>
#include <QKeySequence>

namespace
{
    using Category = HotkeyCatalogEntry::Category;
    using Scope = HotkeyCatalogEntry::Scope;

    // QObject::tr needs a translation context -- "HotkeyCatalog" names this table in .ts files,
    // same posture as QObject::tr(...) calls already made from free functions elsewhere in this
    // codebase (e.g. SettingsDialog.cpp's columnDisplayName()).
    QString hotkeyTr(const char* text)
    {
        return QCoreApplication::translate("HotkeyCatalog", text);
    }

    std::vector<HotkeyCatalogEntry> buildEntries()
    {
        std::vector<HotkeyCatalogEntry> entries;

        // File Operations & Selection (spec §3.6) -- Milestone 1. Every default below matches the
        // key already hardcoded in FileBrowserView::handleKeyPress today (Architecture.md
        // §14.10/§14.11), except SelectAll and Properties, which are net-new keyboard access: today
        // Select All only works via Qt's own QAbstractItemView Ctrl+A default, and Properties only
        // via the context menu. allowsBareRebind is true only for the 6 actions spec §4 names here
        // (Rename, Delete, Delete-permanently, Navigate-to-parent, Open-or-navigate, Properties) --
        // their own shipped defaults are already bare keys, so rebinding them to a *different* bare
        // key must stay possible.
        entries.push_back({ HotkeyAction::Copy, "fileOps.copy", Category::FileOperations, hotkeyTr("Copy"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_C)), Scope::View, false });
        entries.push_back({ HotkeyAction::Cut, "fileOps.cut", Category::FileOperations, hotkeyTr("Cut"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_X)), Scope::View, false });
        entries.push_back({ HotkeyAction::Paste, "fileOps.paste", Category::FileOperations, hotkeyTr("Paste"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_V)), Scope::View, false });
        entries.push_back({ HotkeyAction::Delete, "fileOps.delete", Category::FileOperations,
                             hotkeyTr("Delete (Recycle Bin)"), HotkeyChord(QKeySequence(Qt::Key_Delete)), Scope::View, true });
        entries.push_back({ HotkeyAction::DeletePermanently, "fileOps.deletePermanently", Category::FileOperations,
                             hotkeyTr("Delete permanently"), HotkeyChord(QKeySequence(Qt::SHIFT | Qt::Key_Delete)),
                             Scope::View, true });
        entries.push_back({ HotkeyAction::Rename, "fileOps.rename", Category::FileOperations, hotkeyTr("Rename"),
                             HotkeyChord(QKeySequence(Qt::Key_F2)), Scope::View, true });
        entries.push_back({ HotkeyAction::NavigateToParent, "fileOps.navigateToParent", Category::FileOperations,
                             hotkeyTr("Navigate to parent folder"), HotkeyChord(QKeySequence(Qt::Key_Backspace)),
                             Scope::View, true });
        entries.push_back({ HotkeyAction::OpenOrNavigateInto, "fileOps.openOrNavigateInto", Category::FileOperations,
                             hotkeyTr("Open / navigate into selection"), HotkeyChord(QKeySequence(Qt::Key_Return)),
                             Scope::View, true });
        entries.push_back({ HotkeyAction::SelectAll, "fileOps.selectAll", Category::FileOperations, hotkeyTr("Select all"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_A)), Scope::View, false });
        entries.push_back({ HotkeyAction::Properties, "fileOps.properties", Category::FileOperations, hotkeyTr("Properties"),
                             HotkeyChord(QKeySequence(Qt::ALT | Qt::Key_Return)), Scope::View, true });

        // Navigation (spec §3.1) -- Milestone 2. View-scoped like §3.6 above (spec §5): all five
        // require the file browser view to have focus. None allow a bare rebind -- none of these
        // are in spec §4's bare-key exception list.
        entries.push_back({ HotkeyAction::NavigateBack, "nav.back", Category::Navigation, hotkeyTr("Navigate back"),
                             HotkeyChord(QKeySequence(Qt::ALT | Qt::Key_Left)), Scope::View, false });
        entries.push_back({ HotkeyAction::NavigateForward, "nav.forward", Category::Navigation, hotkeyTr("Navigate forward"),
                             HotkeyChord(QKeySequence(Qt::ALT | Qt::Key_Right)), Scope::View, false });
        entries.push_back({ HotkeyAction::Refresh, "nav.refresh", Category::Navigation, hotkeyTr("Refresh current folder"),
                             HotkeyChord(QKeySequence(Qt::Key_F5)), Scope::View, false });
        entries.push_back({ HotkeyAction::GoToThisPC, "nav.goToThisPC", Category::Navigation, hotkeyTr("Go to \"This PC\""),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Home)), Scope::View, false });
        entries.push_back({ HotkeyAction::FocusAddressBar, "nav.focusAddressBar", Category::Navigation,
                             hotkeyTr("Focus/edit address bar"), HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_L)),
                             Scope::View, false });

        // Tabs (spec §3.2) -- Milestone 3. First Window-scoped category: HotkeyWindowDispatcher
        // resolves these regardless of which widget inside the window currently has focus, acting
        // on whichever WorkspacePaneWidget is currently focused. None allow a bare rebind -- none
        // are in spec §4's bare-key exception list.
        entries.push_back({ HotkeyAction::NewTab, "tabs.new", Category::Tabs, hotkeyTr("New tab"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_T)), Scope::Window, false });
        entries.push_back({ HotkeyAction::CloseTab, "tabs.close", Category::Tabs, hotkeyTr("Close tab"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_W)), Scope::Window, false });
        entries.push_back({ HotkeyAction::NextTab, "tabs.next", Category::Tabs, hotkeyTr("Next tab"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::Key_Tab)), Scope::Window, false });
        entries.push_back({ HotkeyAction::PreviousTab, "tabs.previous", Category::Tabs, hotkeyTr("Previous tab"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab)), Scope::Window, false });

        // Panes & Layout (spec §3.3) -- Milestone 4. Window-scoped like Tabs above. Cycle
        // next/previous allow a bare rebind (spec §4's exception list names them explicitly, their
        // own shipped defaults being F6/Shift+F6); every other row here always ships with a
        // modifier combo, so none of the rest allow a bare rebind.
        entries.push_back({ HotkeyAction::FocusPaneA, "panes.focusA", Category::PanesAndLayout,
                             hotkeyTr("Switch focus to Pane A"), HotkeyChord(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1)),
                             Scope::Window, false });
        entries.push_back({ HotkeyAction::FocusPaneB, "panes.focusB", Category::PanesAndLayout,
                             hotkeyTr("Switch focus to Pane B"), HotkeyChord(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2)),
                             Scope::Window, false });
        entries.push_back({ HotkeyAction::FocusPaneC, "panes.focusC", Category::PanesAndLayout,
                             hotkeyTr("Switch focus to Pane C"), HotkeyChord(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_3)),
                             Scope::Window, false });
        entries.push_back({ HotkeyAction::FocusPaneD, "panes.focusD", Category::PanesAndLayout,
                             hotkeyTr("Switch focus to Pane D"), HotkeyChord(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_4)),
                             Scope::Window, false });
        entries.push_back({ HotkeyAction::CycleNextPane, "panes.cycleNext", Category::PanesAndLayout,
                             hotkeyTr("Cycle focus to next pane"), HotkeyChord(QKeySequence(Qt::Key_F6)), Scope::Window,
                             true });
        entries.push_back({ HotkeyAction::CyclePreviousPane, "panes.cyclePrevious", Category::PanesAndLayout,
                             hotkeyTr("Cycle focus to previous pane"), HotkeyChord(QKeySequence(Qt::SHIFT | Qt::Key_F6)),
                             Scope::Window, true });
        entries.push_back({ HotkeyAction::LayoutSingle, "layout.single", Category::PanesAndLayout,
                             hotkeyTr("Layout: single pane"), HotkeyChord(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1)),
                             Scope::Window, false });
        entries.push_back({ HotkeyAction::LayoutTwoVertical, "layout.twoVertical", Category::PanesAndLayout,
                             hotkeyTr("Layout: two-pane vertical split"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2)), Scope::Window, false });
        entries.push_back({ HotkeyAction::LayoutTwoHorizontal, "layout.twoHorizontal", Category::PanesAndLayout,
                             hotkeyTr("Layout: two-pane horizontal split"),
                             HotkeyChord(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_3)), Scope::Window, false });
        entries.push_back({ HotkeyAction::LayoutFourGrid, "layout.fourGrid", Category::PanesAndLayout,
                             hotkeyTr("Layout: four-pane grid"), HotkeyChord(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_4)),
                             Scope::Window, false });

        return entries;
    }
}

const std::vector<HotkeyCatalogEntry>& HotkeyCatalog::entries()
{
    static const std::vector<HotkeyCatalogEntry> kEntries = buildEntries();
    return kEntries;
}
