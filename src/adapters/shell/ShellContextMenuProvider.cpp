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
    // Command id range for one popup; shell/extensions return offsets in this range from QueryContextMenu.
    // 0 is reserved by TrackPopupMenuEx to mean "cancelled".
    constexpr UINT kCommandFirst = 1;
    constexpr UINT kCommandLast = 0x7FFF;

    // IContextMenu2/3 add HandleMenuMsg(2), needed to route WM_MEASUREITEM/WM_DRAWITEM/WM_MENUCHAR
    // for owner-drawn items back into the shell object. Not all extensions implement them, so we use
    // the best one available and install no handler if neither is present.
    //
    // TrackPopupMenuEx pumps its own private message loop while blocked, so those messages never
    // reach Qt's normal event loop / nativeEvent(). QAbstractNativeEventFilter intercepts native
    // messages before Qt's dispatcher does, including inside nested loops — installed for the
    // duration of one popup only (see runContextMenu).
    class ContextMenuMessageFilter : public QAbstractNativeEventFilter
    {
    public:
        ContextMenuMessageFilter(IContextMenu2* contextMenu2, IContextMenu3* contextMenu3)
            : m_contextMenu2(contextMenu2)
            , m_contextMenu3(contextMenu3)
        {
        }

        // Called by Qt before it processes each native message. This file is Windows-only, so
        // message is always a Win32 MSG*.
        bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override
        {
            Q_UNUSED(eventType);

            if (!m_contextMenu2 && !m_contextMenu3)
            {
                return false;
            }

            auto* msg = static_cast<MSG*>(message);
            // Owner-drawn popup menu messages: WM_INITMENUPOPUP (submenu about to open, e.g. lazy
            // "Send to" population), WM_MEASUREITEM/WM_DRAWITEM (icon sizing/painting), WM_MENUCHAR
            // (unmatched Alt+letter mnemonic). Everything else goes to Qt as normal.
            if (msg->message != WM_INITMENUPOPUP && msg->message != WM_MEASUREITEM && msg->message != WM_DRAWITEM
                && msg->message != WM_MENUCHAR)
            {
                return false;
            }

            // Prefer HandleMenuMsg2 (IContextMenu3): it reports a result, needed for WM_MENUCHAR.
            // Falls back to HandleMenuMsg (IContextMenu2), which has no result out-param.
            LRESULT lresult = 0;
            const HRESULT hr = m_contextMenu3
                ? m_contextMenu3->HandleMenuMsg2(msg->message, msg->wParam, msg->lParam, &lresult)
                : m_contextMenu2->HandleMenuMsg(msg->message, msg->wParam, msg->lParam);
            if (FAILED(hr))
            {
                // Not handled by the shell/extension — let Qt's dispatcher process it instead.
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

    // Shared by both entry points once an IContextMenu is resolved (GetUIObjectOf for a selection,
    // CreateViewObject for empty folder space): builds the popup HMENU, shows it, invokes the chosen
    // command. Always takes ownership of (and releases) contextMenu, regardless of outcome.
    Result<void> runContextMenu(IContextMenu* contextMenu, NativeScreenPoint screenPosition, NativeWindowHandle ownerWindow)
    {
        auto* ownerHwnd = static_cast<HWND>(ownerWindow);

        // Try IContextMenu3 first, fall back to IContextMenu2 (see ContextMenuMessageFilter above).
        // Both left null if neither is supported — the filter treats that as "nothing to forward".
        IContextMenu2* contextMenu2 = nullptr;
        IContextMenu3* contextMenu3 = nullptr;
        contextMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&contextMenu3));
        if (!contextMenu3)
        {
            contextMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&contextMenu2));
        }

        HMENU popupMenu = CreatePopupMenu();
        if (!popupMenu)
        {
            if (contextMenu3) contextMenu3->Release();
            if (contextMenu2) contextMenu2->Release();
            contextMenu->Release();
            return Result<void>::failure(Error(ErrorCode::IoError, "Failed to create a popup menu"));
        }

        // Asks the shell and its extensions to populate popupMenu with entries using ids in
        // [kCommandFirst, kCommandLast]. CMF_NORMAL matches Explorer's ordinary right-click.
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
        QAbstractEventDispatcher* dispatcher = QAbstractEventDispatcher::instance();
        if (dispatcher)
        {
            dispatcher->installNativeEventFilter(&filter);
        }

        // Blocks until the menu closes, pumping its own message loop (why the filter above is needed).
        // TPM_RETURNCMD returns the chosen item id directly instead of posting WM_COMMAND;
        // TPM_RIGHTBUTTON allows selecting with either mouse button.
        const int command = TrackPopupMenuEx(popupMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPosition.x,
                                              screenPosition.y, ownerHwnd, nullptr);

        if (dispatcher)
        {
            dispatcher->removeNativeEventFilter(&filter);
        }
        DestroyMenu(popupMenu);
        if (contextMenu3) contextMenu3->Release();
        if (contextMenu2) contextMenu2->Release();

        Result<void> outcome = Result<void>::success();
        if (command != 0)
        {
            // CMIC_MASK_UNICODE tells the handler to use lpVerbW/lpParametersW over the ANSI fields.
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

    // A PIDL identifies an item in the shell namespace (can point at virtual locations with no
    // filesystem path, e.g. Control Panel). 
    // PIDLIST_ABSOLUTE is rooted at the Desktop;
    // PIDLIST_RELATIVE is relative to an IShellFolder. 
    // Shell-allocated PIDLs must be freed with CoTaskMemFree.
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

    // SHGetDesktopFolder is the mandatory starting point for binding an absolute PIDL to an
    // IShellFolder — there's no free function to resolve a PIDL directly.
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

    // Parse each filename (relative to parentFolder) into a PITEMID_CHILD — a single-segment PIDL,
    // which is what GetUIObjectOf's item array requires.
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

    // GetUIObjectOf wants LPCITEMIDLIST elements; copy into a second vector since MSVC won't
    // implicitly add const through a pointer-to-pointer cast.
    std::vector<LPCITEMIDLIST> constChildPidls(childPidls.begin(), childPidls.end());

    // GetUIObjectOf returns a COM object for the selection as a whole (one IContextMenu spanning
    // all items, matching how Explorer resolves a multi-item selection). IID_IContextMenu picks
    // which interface to request; the same call could ask for IExtractIcon, IDataObject, etc.
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
    // Same desktop-root-then-bind pattern as showItemContextMenu, but resolves the folder itself
    // (an empty-space right-click belongs to the folder, not to any item inside it).
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

    // CreateViewObject is the background counterpart to GetUIObjectOf: it returns a COM object for
    // the folder's own view rather than for child items (not GetUIObjectOf with zero items — a
    // folder's view is a distinct object in the shell's model). This IContextMenu supplies
    // New/Paste/Sort by/Refresh/Properties for the folder itself.
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
