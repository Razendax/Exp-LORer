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

        return entries;
    }
}

const std::vector<HotkeyCatalogEntry>& HotkeyCatalog::entries()
{
    static const std::vector<HotkeyCatalogEntry> kEntries = buildEntries();
    return kEntries;
}
