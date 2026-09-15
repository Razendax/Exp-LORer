#include "ShellContextMenuProvider.h"

#ifdef _WIN32
#include <Windows.h>

#include <shlobj.h>
#include <shobjidl.h>

#include <QAbstractEventDispatcher>
#include <QAbstractNativeEventFilter>
#endif

namespace
{
    namespace fs = std::filesystem;

#ifdef _WIN32
    // Offset range existing for the duration of one popup. Used by QueryContextMenu/TrackPopupMenuEx
    // Shell or other extensions insert their items into this range and return offsets within it.
    // 0 is reserved by TrackPopupMenuEx to mean "menu was cancelled".
    constexpr UINT kCommandFirst = 1;
    constexpr UINT kCommandLast = 0x7FFF;

    // IContextMenu - is interface QueryContextMenu/InvokeCommand live on and is all every shell context menu MUST to implement.
    // IContextMenu2 - adds HandleMenuMsg (needed once a menu can contain owner-drawn items — icons, 
    // separators, cascading submenus — because the WM_MEASUREITEM/WM_DRAWITEM/etc. messages Windows sends 
    // for those must be routed back into the COM object that owns the menu). 
    // IContextMenu3 - is the same idea with an extra out-parameter (HandleMenuMsg2) so the object can report a message result
    // (needed for WM_MENUCHAR's mnemonic-key handling). 
    // Not every shell extension implements the newer interfaces, so the best one available used, falling back to
    // "handle nothing" if neither is present (menus without owner-drawn/extension items still work fine with no handler installed at all).
    //
    // This class exists because those HandleMenuMsg(2) calls have to happen for messages that
    // arrive *while TrackPopupMenuEx is blocked pumping its own private message loop* — those messages never reach Qt's normal event loop,
    // so regular QObject::event()/QWidget::nativeEvent() override would never see them.
    // QAbstractNativeEventFilter is Qt's hook for intercepting *every* native (Win32) message
    // before Qt's dispatcher touches it, including ones delivered inside a nested loop.
    // It's installed only for the duration of one popup (see runContextMenu) and removed immediately after.
    class ContextMenuMessageFilter : public QAbstractNativeEventFilter
    {
    public:
        ContextMenuMessageFilter(IContextMenu2* contextMenu2, IContextMenu3* contextMenu3)
            : m_contextMenu2(contextMenu2)
            , m_contextMenu3(contextMenu3)
        {
        }

        // Called by Qt for every native message on every thread with an event dispatcher, before Qt itself processes it.
        // eventType distinguishes which native message format this is
        // ("windows_generic_MSG" on Windows, others on other platforms) — this file only runs on
        // Windows, so message is always safe to treat as a Win32 MSG*.
        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
        {
            Q_UNUSED(eventType);

            if (!m_contextMenu2 && !m_contextMenu3)
            {
                return false;
            }

            auto* msg = static_cast<MSG*>(message);
            // The four messages Windows sends to the *owner* window for an owner-drawn popup menu:
            // - WM_INITMENUPOPUP (a submenu is about to open — lets shell extensions lazily populate cascading submenus like "Send to"),
            // - WM_MEASUREITEM/WM_DRAWITEM (owner-draw sizing/painting, e.g. for icons next to entries),
            // - WM_MENUCHAR (an Alt+letter mnemonic was pressed that doesn't match any item, giving the menu owner a chance to resolve it).
            // - Anything else is left for Qt to handle normally.
            if (msg->message != WM_INITMENUPOPUP && msg->message != WM_MEASUREITEM && msg->message != WM_DRAWITEM
                && msg->message != WM_MENUCHAR)
            {
                return false;
            }

            // Prefer IContextMenu3::HandleMenuMsg2 (reports a result via the out-param, needed
            // for WM_MENUCHAR to say which item it resolved to);
            // Fall back to the older IContextMenu2::HandleMenuMsg, which handles the message but never reports a result.
            LRESULT lresult = 0;
            const HRESULT hr = m_contextMenu3
                ? m_contextMenu3->HandleMenuMsg2(msg->message, msg->wParam, msg->lParam, &lresult)
                : m_contextMenu2->HandleMenuMsg(msg->message, msg->wParam, msg->lParam);
            if (FAILED(hr))
            {
                // The shell/extension didn't want this particular message; returning false here
                // lets Qt's own dispatcher process it instead of swallowing it silently.
                return false;
            }

            if (result)
            {
                *result = static_cast<qintptr>(lresult);
            }
            return true; // handled — stop Qt from processing this message further.
        }

    private:
        IContextMenu2* m_contextMenu2 = nullptr;
        IContextMenu3* m_contextMenu3 = nullptr;
    };

    // Shared by both entry points once an IContextMenu has been resolved (from GetUIObjectOf for
    // a file/folder selection, or CreateViewObject for empty-space-in-a-folder.
    // Builds the actual popup HMENU, shows it, and invokes whatever the user picked. 
    // Cancelling the menu (TrackPopupMenuEx returns 0 with no command)
    // Always takes ownership of (and releases) contextMenu, regardless of outcome.
    Result<void> runContextMenu(IContextMenu* contextMenu, NativeScreenPoint screenPosition, NativeWindowHandle ownerWindow)
    {
        auto* ownerHwnd = static_cast<HWND>(ownerWindow);

        // QueryInterface is COM's "ask this object if it also implements a different interface" call.
        // Trying IContextMenu3 first and only falling back to IContextMenu2 mirrors the versioning note
        // on ContextMenuMessageFilter above: 
        //    newer/better interface if present, otherwise the older one,
        //    otherwise no owner-draw message routing at all (leaves *ContextMenu2/3
        //    nullptr, which ContextMenuMessageFilter treats as "nothing to forward to").
        IContextMenu2* contextMenu2 = nullptr;
        IContextMenu3* contextMenu3 = nullptr;
        contextMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&contextMenu3));
        if (!contextMenu3)
        {
            contextMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&contextMenu2));
        }

        // CreatePopupMenu makes an empty native Win32 menu handle (HMENU) — just a container at this point; 
        HMENU popupMenu = CreatePopupMenu();
        if (!popupMenu)
        {
            if (contextMenu3) contextMenu3->Release();
            if (contextMenu2) contextMenu2->Release();
            contextMenu->Release();
            return Result<void>::failure(Error(ErrorCode::IoError, "Failed to create a popup menu"));
        }

        // QueryContextMenu fills popupMenu in with the shell's items.
        // Asks the shell (and any registered shell extensions for this item/folder ) to populate
        // popupMenu with their entries, at menu position 0 (top), using command ids in
        // [kCommandFirst, kCommandLast]. CMF_NORMAL is the same flag set Explorer itself uses for
        // an ordinary right-click (as opposed to e.g. CMF_EXPLORE or a Shift-held "extended" menu variant)
        HRESULT hr = contextMenu->QueryContextMenu(popupMenu, 0, kCommandFirst, kCommandLast, CMF_NORMAL);
        if (FAILED(hr))
        {
            DestroyMenu(popupMenu);
            if (contextMenu3) contextMenu3->Release();
            if (contextMenu2) contextMenu2->Release();
            contextMenu->Release();
            return Result<void>::failure(Error(ErrorCode::IoError, "Failed to build the shell context menu"));
        }

        // Installed only around the call that actually pumps messages (TrackPopupMenuEx below);
        // see the long comment on ContextMenuMessageFilter for why this is necessary at all.
        ContextMenuMessageFilter filter(contextMenu2, contextMenu3);
        QAbstractEventDispatcher::instance()->installNativeEventFilter(&filter);

        // TrackPopupMenuEx - show popup menu at a screen position.
        // Blocks calling thread until menu closes, running internal message loop (reason why QAbstractNativeEventFilter is needed).
        // TPM_RETURNCMD: returns the menu item identifier of the user's selection in the return value.
        //                instead of posting a WM_COMMAND for the chosen item, 
        // TPM_RIGHTBUTTON: The user can select menu items with both the left and right mouse buttons.
        const int command = TrackPopupMenuEx(popupMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPosition.x,
                                              screenPosition.y, ownerHwnd, nullptr);

        QAbstractEventDispatcher::instance()->removeNativeEventFilter(&filter);
        DestroyMenu(popupMenu);
        if (contextMenu3) contextMenu3->Release();
        if (contextMenu2) contextMenu2->Release();

        Result<void> outcome = Result<void>::success();
        if (command != 0)
        {
            // CMINVOKECOMMANDINFOEX ("EX" = extended) Contains extended information about a shortcut menu command
            // CMIC_MASK_UNICODE - The shortcut menu handler should use lpVerbW, lpParametersW ... members instead of their ANSI equivalents
            CMINVOKECOMMANDINFOEX info{};
            info.cbSize = sizeof(info);
            info.fMask = CMIC_MASK_UNICODE;
            info.hwnd = ownerHwnd;
            info.lpVerb = MAKEINTRESOURCEA(command - kCommandFirst);
            info.lpVerbW = MAKEINTRESOURCEW(command - kCommandFirst);
            info.nShow = SW_SHOWNORMAL; // in case the verb launches a window (e.g. "Properties").

            hr = contextMenu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
            if (FAILED(hr))
            {
                outcome = Result<void>::failure(Error(ErrorCode::IoError, "Failed to invoke the shell context menu command"));
            }
        }

        contextMenu->Release();
        return outcome;
    }

    // A PIDL (pointer to an ITEMIDLIST) is the shell namespace's universal item identifier (can point into virtual namespace locations that have
    // no filesystem path at all, like Control Panel or a zip file's contents, shell uses it instead of strings internally).
    // PIDLIST_ABSOLUTE means "rooted at the Desktop, the namespace's root"
    // PIDLIST_RELATIVE means "relative to some specific IShellFolder"
    // SHParseDisplayName - parses normal path string into a PIDL
    // PIDL returned by shell APIs is allocated with the shell's task allocator and must be freed with CoTaskMemFree.
    Result<PIDLIST_ABSOLUTE> parseAbsolutePidl(const fs::path& path)
    {
        PIDLIST_ABSOLUTE pidl = nullptr;
        const HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
        if (FAILED(hr) || !pidl)
        {
            return Result<PIDLIST_ABSOLUTE>::failure(Error(ErrorCode::NotFound, "Failed to resolve " + path.string()));
        }
        return Result<PIDLIST_ABSOLUTE>::success(pidl);
    }
#endif
}

#ifdef _WIN32

Result<void> ShellContextMenuProvider::showItemContextMenu(const std::vector<fs::path>& paths, NativeScreenPoint screenPosition,
                                                             NativeWindowHandle ownerWindow)
{
    if (paths.empty())
    {
        return Result<void>::failure(Error(ErrorCode::InvalidArgument, "No items to show a context menu for"));
    }

    const fs::path parentDirectory = paths.front().parent_path();
    for (const auto& path : paths)
    {
        if (path.parent_path() != parentDirectory)
        {
            return Result<void>::failure(
                Error(ErrorCode::InvalidArgument, "All items must share one parent directory"));
        }
    }

    // SHGetDesktopFolder returns the IShellFolder for the Desktop, is mandatory starting point for resolving any absolute PIDL into a bound
    // IShellFolder object (there's no "resolve this PIDL" free function; you always walk down
    // from a folder you already have a live interface to)
    IShellFolder* desktop = nullptr;
    if (FAILED(SHGetDesktopFolder(&desktop)) || !desktop)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to get the desktop shell folder"));
    }

    // Resolve the *parent directory* first
    auto parentPidlResult = parseAbsolutePidl(parentDirectory);
    if (parentPidlResult.hasError())
    {
        desktop->Release();
        return Result<void>::failure(std::move(parentPidlResult).error());
    }
    PIDLIST_ABSOLUTE parentPidl = parentPidlResult.value();

    // BindToObject walks from desktop down to the folder identified by parentPidl and returns a
    // *new* IShellFolder interface representing it
    IShellFolder* parentFolder = nullptr;
    const HRESULT bindResult = desktop->BindToObject(parentPidl, nullptr, IID_IShellFolder, reinterpret_cast<void**>(&parentFolder));
    CoTaskMemFree(parentPidl);
    desktop->Release();
    if (FAILED(bindResult) || !parentFolder)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to bind the parent shell folder"));
    }

    // For each selected item, ask the parent folder to parse just its filename into a *relative* PIDL (PIDLIST_RELATIVE)
    // PITEMID_CHILD is the same relative-PIDL concept further specialized to "exactly one path segment, no nested sub-
    // path" — what GetUIObjectOf specifically requires for its item array
    std::vector<PITEMID_CHILD> childPidls;
    childPidls.reserve(paths.size());
    for (const auto& path : paths)
    {
        std::wstring name = path.filename().wstring();
        PIDLIST_RELATIVE childPidl = nullptr;
        const HRESULT hr = parentFolder->ParseDisplayName(static_cast<HWND>(ownerWindow), nullptr, name.data(),
                                                            nullptr, &childPidl, nullptr);
        if (FAILED(hr) || !childPidl)
        {
            for (auto* pidl : childPidls)
            {
                CoTaskMemFree(pidl);
            }
            parentFolder->Release();
            return Result<void>::failure(Error(ErrorCode::NotFound, "Failed to resolve item: " + path.string()));
        }
        childPidls.push_back(reinterpret_cast<PITEMID_CHILD>(childPidl));
    }

    // GetUIObjectOf wants the child array as LPCITEMIDLIST (const-pointer) elements; copy into a
    // second vector of that type rather than reinterpret_cast'ing the whole array in place, since
    // MSVC (rightly) won't implicitly add const through a pointer-to-pointer cast.
    std::vector<LPCITEMIDLIST> constChildPidls(childPidls.begin(), childPidls.end());

    // GetUIObjectOf is the actual "give me a COM object representing this selection" call — the
    // shell-namespace equivalent of instantiating a right-click handler for one or more sibling
    // items at once (hence it takes an array: a real multi-item Explorer selection also resolves
    // to a single IContextMenu spanning all of them, not one per item). IID_IContextMenu picks
    // which interface on that object is wanted; the same method is also how a caller would ask
    // for e.g. IExtractIcon or IDataObject instead, for other purposes this codebase doesn't need.
    IContextMenu* contextMenu = nullptr;
    const HRESULT uiObjectResult = parentFolder->GetUIObjectOf(
        static_cast<HWND>(ownerWindow), static_cast<UINT>(constChildPidls.size()), constChildPidls.data(),
        IID_IContextMenu, nullptr, reinterpret_cast<void**>(&contextMenu));

    for (auto* pidl : childPidls)
    {
        CoTaskMemFree(pidl);
    }
    parentFolder->Release();

    if (FAILED(uiObjectResult) || !contextMenu)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to get the item context menu"));
    }

    return runContextMenu(contextMenu, screenPosition, ownerWindow);
}

Result<void> ShellContextMenuProvider::showBackgroundContextMenu(const fs::path& folder, NativeScreenPoint screenPosition,
                                                                   NativeWindowHandle ownerWindow)
{
    // Same desktop-root-then-bind pattern as showItemContextMenu above, except this time the
    // PIDL/IShellFolder resolved is *the folder itself* (there's no separate "parent" here — an
    // empty-space right-click is a property of the folder being viewed, not of anything inside
    // it).
    IShellFolder* desktop = nullptr;
    if (FAILED(SHGetDesktopFolder(&desktop)) || !desktop)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to get the desktop shell folder"));
    }

    auto folderPidlResult = parseAbsolutePidl(folder);
    if (folderPidlResult.hasError())
    {
        desktop->Release();
        return Result<void>::failure(std::move(folderPidlResult).error());
    }
    PIDLIST_ABSOLUTE folderPidl = folderPidlResult.value();

    IShellFolder* shellFolder = nullptr;
    const HRESULT bindResult = desktop->BindToObject(folderPidl, nullptr, IID_IShellFolder, reinterpret_cast<void**>(&shellFolder));
    CoTaskMemFree(folderPidl);
    desktop->Release();
    if (FAILED(bindResult) || !shellFolder)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to bind the folder's shell folder"));
    }

    // CreateViewObject is the folder-background counterpart to GetUIObjectOf above: instead of
    // "give me a COM object for these child items", it's "give me a COM object for this folder's
    // own view" — a different object model in the shell's design (a folder's Explorer window is
    // itself represented by a view object, separate from any item inside it), which is why this
    // is a different method entirely rather than GetUIObjectOf called with zero items. The
    // IContextMenu obtained this way is what supplies New/Paste/Sort by/Refresh/Properties-of-
    // this-folder — entries that belong to the folder as a whole, not to any selection within it.
    IContextMenu* contextMenu = nullptr;
    const HRESULT viewObjectResult = shellFolder->CreateViewObject(static_cast<HWND>(ownerWindow), IID_IContextMenu,
                                                                     reinterpret_cast<void**>(&contextMenu));
    shellFolder->Release();

    if (FAILED(viewObjectResult) || !contextMenu)
    {
        return Result<void>::failure(Error(ErrorCode::IoError, "Failed to get the folder background context menu"));
    }

    return runContextMenu(contextMenu, screenPosition, ownerWindow);
}

#else

Result<void> ShellContextMenuProvider::showItemContextMenu(const std::vector<fs::path>&, NativeScreenPoint, NativeWindowHandle)
{
    return Result<void>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

Result<void> ShellContextMenuProvider::showBackgroundContextMenu(const fs::path&, NativeScreenPoint, NativeWindowHandle)
{
    return Result<void>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

#endif
