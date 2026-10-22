#include "ShellContextMenuProvider.h"

#include <algorithm>
#include <cwctype>
#include <set>
#include <string>

#include "Logging.h"

#ifdef _WIN32
#include <Windows.h>

#include <Shellapi.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <Shobjidl.h>
#include <Commctrl.h>
#endif

namespace
{
    namespace fs = std::filesystem;

#ifdef _WIN32
    // The app's own spliced-in actions live at [kAppActionIdFirst, kAppActionIdLast] -- clear of
    // the shell's own idCmdFirst/idCmdLast range ([1, 0x7000]) passed to QueryContextMenu below.
    constexpr UINT kShellIdFirst = 1;
    constexpr UINT kShellIdLast = 0x7000;
    constexpr std::uint32_t kAppActionIdFirst = 0x7001;

    // Every call below hands control to arbitrary shell-extension code running in-process -- not
    // just third-party installs, but Windows' own: a right-click on any ordinary NTFS folder was
    // observed (via a WER minidump) to crash inside cscui.dll's Offline Files/Client-Side-Caching
    // IContextMenu handler (cscui!_CmdIDToMap -> an out-of-bounds KERNELBASE!StrCmpICW read) the
    // moment GetCommandString asked it about an idCmd it didn't expect. Explorer itself guards
    // every one of these entry points the same way for exactly this reason -- a single misbehaving
    // (or, as here, buggy-but-built-in) handler must not be able to take the whole app down. Each
    // wrapper below is its own function (not inlined into a caller holding RAII members) because
    // MSVC forbids __try in a function that also needs C++ object unwinding (C2712).
    HRESULT guardedQueryContextMenu(IContextMenu* contextMenu, HMENU menu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast,
                                     UINT flags)
    {
        __try
        {
            return contextMenu->QueryContextMenu(menu, indexMenu, idCmdFirst, idCmdLast, flags);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return E_FAIL;
        }
    }

    HRESULT guardedGetCommandString(IContextMenu* contextMenu, UINT_PTR idCmd, UINT uType, LPSTR name, UINT cchMax)
    {
        __try
        {
            return contextMenu->GetCommandString(idCmd, uType, nullptr, name, cchMax);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return E_FAIL;
        }
    }

    HRESULT guardedInvokeCommand(IContextMenu* contextMenu, CMINVOKECOMMANDINFOEX* info)
    {
        __try
        {
            return contextMenu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(info));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return E_FAIL;
        }
    }

    HRESULT guardedGetUIObjectOf(IShellFolder* folder, HWND ownerHwnd, UINT count, LPCITEMIDLIST* pidls, REFIID riid, void** ppv)
    {
        __try
        {
            return folder->GetUIObjectOf(ownerHwnd, count, pidls, riid, nullptr, ppv);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return E_FAIL;
        }
    }

    HRESULT guardedCreateViewObject(IShellFolder* folder, HWND ownerHwnd, REFIID riid, void** ppv)
    {
        __try
        {
            return folder->CreateViewObject(ownerHwnd, riid, ppv);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return E_FAIL;
        }
    }

    HRESULT guardedGetItemObject(IShellView* shellView, UINT itemFlags, REFIID riid, void** ppv)
    {
        __try
        {
            return shellView->GetItemObject(itemFlags, riid, ppv);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return E_FAIL;
        }
    }

    std::wstring wideFromUtf8(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);
        return result;
    }

    // Cut/Copy/Paste/Delete/Rename/Properties/default-Open have app-native equivalents supplied via
    // NativeMenuLayout -- shell-contributed top-level items with these canonical verbs are removed
    // so they're not shown twice (Architecture.md §14.102, reused unchanged from §14.13.3's
    // suppressedVerbNames()). Nested cascade items are never suppressed, same as Explorer itself
    // never touches them.
    const std::set<std::wstring>& suppressedVerbNames()
    {
        static const std::set<std::wstring> names = { L"open", L"cut", L"copy", L"paste", L"delete", L"rename", L"properties" };
        return names;
    }

    std::wstring toLowerCopy(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return text;
    }

    // Removes every top-level item whose canonical verb (GCS_VERBW) is in suppressedVerbNames().
    // Walks backwards since RemoveMenu shifts subsequent indices down.
    void suppressDuplicatedVerbs(HMENU menu, IContextMenu* contextMenu)
    {
        for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i)
        {
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_ID | MIIM_FTYPE;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info) || (info.fType & MFT_SEPARATOR) || info.wID == 0)
            {
                continue;
            }

            wchar_t verb[256] = {};
            if (FAILED(guardedGetCommandString(contextMenu, info.wID - kShellIdFirst, GCS_VERBW, reinterpret_cast<char*>(verb),
                                                 static_cast<UINT>(std::size(verb)))))
            {
                continue;
            }

            if (suppressedVerbNames().count(toLowerCopy(verb)) != 0)
            {
                RemoveMenu(menu, static_cast<UINT>(i), MF_BYPOSITION);
            }
        }
    }

    // Inserts one NativeMenuAction (or a plain separator) at `position`. MIIM_ID|MIIM_STATE|
    // MIIM_STRING|MIIM_FTYPE only -- no icon for these, a deliberate v1 cut (Architecture.md
    // §14.102.6).
    void insertActionAt(HMENU menu, UINT position, const NativeMenuAction& action, std::wstring& labelStorage)
    {
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);

        if (action.isSeparator)
        {
            item.fMask = MIIM_FTYPE;
            item.fType = MFT_SEPARATOR;
        }
        else
        {
            labelStorage = wideFromUtf8(action.label);
            item.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_STRING;
            item.fType = MFT_STRING;
            item.fState = action.enabled ? MFS_ENABLED : MFS_DISABLED;
            item.wID = action.id;
            item.dwTypeData = labelStorage.data();
            item.cch = static_cast<UINT>(labelStorage.size());
        }

        InsertMenuItemW(menu, position, TRUE, &item);
    }

    // Splices `layout`'s groups into the already-shell-populated `menu`: top/clipboard groups go in
    // ahead of the shell's own items (each followed by one separator), bottom/final groups are
    // appended at the end (each preceded by one separator) -- the same conventional positions
    // ContextMenuBuilder hardcoded before §14.102.
    void mergeNativeLayout(HMENU menu, const NativeMenuLayout& layout)
    {
        // Backing storage for every inserted label -- MENUITEMINFOW::dwTypeData must stay valid
        // only for the duration of each InsertMenuItemW call, but keeping one slot per inserted
        // action alive until mergeNativeLayout returns is simplest and cheap (menu sizes are small).
        std::vector<std::wstring> labelStorage;
        labelStorage.reserve(layout.topActions.size() + layout.clipboardActions.size() + layout.bottomActions.size() +
                              layout.finalActions.size());

        UINT position = 0;
        const auto insertFront = [&](const std::vector<NativeMenuAction>& group) {
            if (group.empty())
            {
                return;
            }
            for (const NativeMenuAction& action : group)
            {
                labelStorage.emplace_back();
                insertActionAt(menu, position++, action, labelStorage.back());
            }
            NativeMenuAction separator;
            separator.isSeparator = true;
            labelStorage.emplace_back();
            insertActionAt(menu, position++, separator, labelStorage.back());
        };

        insertFront(layout.topActions);
        insertFront(layout.clipboardActions);

        const auto appendBack = [&](const std::vector<NativeMenuAction>& group) {
            if (group.empty())
            {
                return;
            }
            UINT tail = static_cast<UINT>(GetMenuItemCount(menu));
            NativeMenuAction separator;
            separator.isSeparator = true;
            labelStorage.emplace_back();
            insertActionAt(menu, tail++, separator, labelStorage.back());
            for (const NativeMenuAction& action : group)
            {
                labelStorage.emplace_back();
                insertActionAt(menu, tail++, action, labelStorage.back());
            }
        };

        appendBack(layout.bottomActions);
        appendBack(layout.finalActions);
    }

    // ---- Message forwarding while the popup is tracking (Architecture.md §14.102.3) -------------

    struct MenuMsgForwarder
    {
        IContextMenu2* contextMenu2 = nullptr;
        IContextMenu3* contextMenu3 = nullptr;
    };

    constexpr UINT_PTR kMenuMsgSubclassId = 1;

    LRESULT CALLBACK menuMsgSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*subclassId*/,
                                          DWORD_PTR refData)
    {
        auto* forwarder = reinterpret_cast<MenuMsgForwarder*>(refData);
        switch (msg)
        {
            case WM_INITMENUPOPUP:
            case WM_MEASUREITEM:
            case WM_DRAWITEM:
            case WM_MENUCHAR:
            case WM_UNINITMENUPOPUP:
            {
                LRESULT result = 0;
                HRESULT hr = E_FAIL;
                if (forwarder->contextMenu3)
                {
                    __try
                    {
                        hr = forwarder->contextMenu3->HandleMenuMsg2(msg, wParam, lParam, &result);
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        hr = E_FAIL;
                    }
                    if (SUCCEEDED(hr))
                    {
                        return result;
                    }
                }
                if (forwarder->contextMenu2)
                {
                    __try
                    {
                        hr = forwarder->contextMenu2->HandleMenuMsg(msg, wParam, lParam);
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        hr = E_FAIL;
                    }
                    if (SUCCEEDED(hr))
                    {
                        return 0;
                    }
                }
                break;
            }
            default:
                break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    // RAII guard so the subclass comes off on every exit path (including an exception) -- installed
    // only for the duration of TrackPopupMenuEx.
    class MenuMsgSubclassGuard
    {
    public:
        MenuMsgSubclassGuard(HWND hwnd, MenuMsgForwarder& forwarder) : m_hwnd(hwnd)
        {
            m_installed = SetWindowSubclass(hwnd, menuMsgSubclassProc, kMenuMsgSubclassId, reinterpret_cast<DWORD_PTR>(&forwarder));
        }

        ~MenuMsgSubclassGuard()
        {
            if (m_installed)
            {
                RemoveWindowSubclass(m_hwnd, menuMsgSubclassProc, kMenuMsgSubclassId);
            }
        }

        MenuMsgSubclassGuard(const MenuMsgSubclassGuard&) = delete;
        MenuMsgSubclassGuard& operator=(const MenuMsgSubclassGuard&) = delete;

    private:
        HWND m_hwnd;
        BOOL m_installed = FALSE;
    };

    // Shows `menu` at screenPos, forwarding the handful of window messages shell extensions need
    // (lazily-populated cascades, owner-drawn icons) to contextMenu's IContextMenu2/3 while it's
    // tracking, and returns the chosen command id (0 == dismissed without picking anything).
    UINT trackMenu(HMENU menu, IContextMenu* contextMenu, HWND ownerHwnd, NativeMenuPoint screenPos)
    {
        MenuMsgForwarder forwarder;
        contextMenu->QueryInterface(IID_IContextMenu3, reinterpret_cast<void**>(&forwarder.contextMenu3));
        if (!forwarder.contextMenu3)
        {
            contextMenu->QueryInterface(IID_IContextMenu2, reinterpret_cast<void**>(&forwarder.contextMenu2));
        }

        {
            MenuMsgSubclassGuard guard(ownerHwnd, forwarder);
            const UINT chosen = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                                      screenPos.x, screenPos.y, ownerHwnd, nullptr));
            if (forwarder.contextMenu3)
            {
                forwarder.contextMenu3->Release();
            }
            if (forwarder.contextMenu2)
            {
                forwarder.contextMenu2->Release();
            }
            return chosen;
        }
    }

    // Dispatches the id TrackPopupMenuEx returned: 0 => Cancelled; within the app's own reserved
    // range => AppAction; anything else => invoke it on the shell's IContextMenu and report
    // ShellCommandInvoked. `workingDirectory` is the item's parent folder (item menu) or the
    // folder itself (background menu).
    Result<ContextMenuResult> dispatchChosenId(UINT chosenId, IContextMenu* contextMenu, HWND ownerHwnd, NativeMenuPoint screenPos,
                                                 const fs::path& workingDirectory)
    {
        if (chosenId == 0)
        {
            return Result<ContextMenuResult>::success(ContextMenuResult{ ContextMenuOutcome::Cancelled, std::nullopt });
        }

        if (chosenId >= kAppActionIdFirst)
        {
            return Result<ContextMenuResult>::success(
                ContextMenuResult{ ContextMenuOutcome::AppAction, static_cast<std::uint32_t>(chosenId) });
        }

        CMINVOKECOMMANDINFOEX info{};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        info.hwnd = ownerHwnd;
        info.lpVerb = MAKEINTRESOURCEA(chosenId - kShellIdFirst);
        info.lpVerbW = MAKEINTRESOURCEW(chosenId - kShellIdFirst);
        info.nShow = SW_SHOWNORMAL;
        info.ptInvoke = { screenPos.x, screenPos.y };

        // Registry-registered static verbs commonly use "%V" in their command line to mean "the
        // current folder" (observed with "Open with VS Code"/"Open in Zed"-style folder commands,
        // but also e.g. the built-in "Open PowerShell window here"). That substitution is resolved
        // from this field, not from whatever PIDL(s) the IContextMenu itself was built against --
        // leaving it unset collapses %V to nothing, which several handlers then treat as "open this
        // (empty) target with its default app", surfacing Windows' own "This file does not have an
        // app associated with it" dialog instead of actually launching anything.
        const std::wstring workingDirectoryWide = workingDirectory.wstring();
        info.lpDirectoryW = workingDirectoryWide.c_str();

        // Never log which command was invoked -- id alone doesn't leak paths, but keep the same
        // logging posture as every other shell-invoke path in this adapter (Architecture.md §14.23).
        Logging::debug("ShellContextMenuProvider: shell command invoked");
        const HRESULT hr = guardedInvokeCommand(contextMenu, &info);
        if (FAILED(hr))
        {
            Logging::warn("ShellContextMenuProvider: shell command invocation failed");
            return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to invoke the shell command"));
        }

        return Result<ContextMenuResult>::success(ContextMenuResult{ ContextMenuOutcome::ShellCommandInvoked, std::nullopt });
    }

    // RAII for the handful of COM/GDI resources every build+show call acquires, so every early
    // return (validation failure, failed bind, etc.) still releases them.
    struct ComMenuScope
    {
        IShellFolder* desktop = nullptr;
        IShellFolder* targetFolder = nullptr; // parent folder (item menu) or the folder itself (background menu).
        IContextMenu* contextMenu = nullptr;
        HMENU menu = nullptr;
        PIDLIST_ABSOLUTE rootPidl = nullptr;       // parent (item menu) or folder (background menu) absolute PIDL.
        std::vector<PITEMID_CHILD> childPidls;      // item menu only.

        ~ComMenuScope()
        {
            if (menu)
            {
                DestroyMenu(menu);
            }
            if (contextMenu)
            {
                contextMenu->Release();
            }
            for (auto* pidl : childPidls)
            {
                CoTaskMemFree(pidl);
            }
            if (rootPidl)
            {
                CoTaskMemFree(rootPidl);
            }
            if (targetFolder)
            {
                targetFolder->Release();
            }
            if (desktop)
            {
                desktop->Release();
            }
        }
    };
#endif
}

#ifdef _WIN32

Result<ContextMenuResult> ShellContextMenuProvider::showItemMenu(const std::vector<fs::path>& paths, const NativeMenuLayout& layout,
                                                                    NativeWindowHandle ownerWindow, NativeMenuPoint screenPos)
{
    if (paths.empty())
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::InvalidArgument, "No items to build a context menu for"));
    }

    const fs::path parentDirectory = paths.front().parent_path();
    for (const auto& path : paths)
    {
        if (path.parent_path() != parentDirectory)
        {
            return Result<ContextMenuResult>::failure(Error(ErrorCode::InvalidArgument, "All items must share one parent directory"));
        }
    }

    ComMenuScope scope;
    if (FAILED(SHGetDesktopFolder(&scope.desktop)) || !scope.desktop)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to resolve the desktop shell folder"));
    }

    if (FAILED(SHParseDisplayName(parentDirectory.c_str(), nullptr, &scope.rootPidl, 0, nullptr)) || !scope.rootPidl)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to resolve the item's parent folder"));
    }

    if (FAILED(scope.desktop->BindToObject(scope.rootPidl, nullptr, IID_IShellFolder, reinterpret_cast<void**>(&scope.targetFolder)))
        || !scope.targetFolder)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to bind the item's parent folder"));
    }

    for (const auto& path : paths)
    {
        std::wstring name = path.filename().wstring();
        PIDLIST_RELATIVE childPidl = nullptr;
        if (SUCCEEDED(scope.targetFolder->ParseDisplayName(nullptr, nullptr, name.data(), nullptr, &childPidl, nullptr)) && childPidl)
        {
            scope.childPidls.push_back(reinterpret_cast<PITEMID_CHILD>(childPidl));
        }
    }

    if (scope.childPidls.empty())
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to resolve the selected item(s)"));
    }

    std::vector<LPCITEMIDLIST> constChildPidls(scope.childPidls.begin(), scope.childPidls.end());
    const auto ownerHwnd = static_cast<HWND>(ownerWindow);
    if (FAILED(guardedGetUIObjectOf(scope.targetFolder, ownerHwnd, static_cast<UINT>(constChildPidls.size()), constChildPidls.data(),
                                      IID_IContextMenu, reinterpret_cast<void**>(&scope.contextMenu)))
        || !scope.contextMenu)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to build the item's context menu"));
    }

    scope.menu = CreatePopupMenu();
    if (!scope.menu)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to create the popup menu"));
    }

    if (FAILED(guardedQueryContextMenu(scope.contextMenu, scope.menu, 0, kShellIdFirst, kShellIdLast, CMF_NORMAL)))
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to query the item's context menu"));
    }

    suppressDuplicatedVerbs(scope.menu, scope.contextMenu);
    mergeNativeLayout(scope.menu, layout);

    Logging::debug("ShellContextMenuProvider: item context menu shown");
    const UINT chosenId = trackMenu(scope.menu, scope.contextMenu, ownerHwnd, screenPos);
    return dispatchChosenId(chosenId, scope.contextMenu, ownerHwnd, screenPos, parentDirectory);
}

Result<ContextMenuResult> ShellContextMenuProvider::showBackgroundMenu(const fs::path& folder, const NativeMenuLayout& layout,
                                                                          NativeWindowHandle ownerWindow, NativeMenuPoint screenPos)
{
    ComMenuScope scope;
    if (FAILED(SHGetDesktopFolder(&scope.desktop)) || !scope.desktop)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to resolve the desktop shell folder"));
    }

    if (FAILED(SHParseDisplayName(folder.c_str(), nullptr, &scope.rootPidl, 0, nullptr)) || !scope.rootPidl)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to resolve the folder"));
    }

    if (FAILED(scope.desktop->BindToObject(scope.rootPidl, nullptr, IID_IShellFolder, reinterpret_cast<void**>(&scope.targetFolder)))
        || !scope.targetFolder)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to bind the folder"));
    }

    const auto ownerHwnd = static_cast<HWND>(ownerWindow);

    // Going through IShellView::GetItemObject(SVGIO_BACKGROUND, ...) -- rather than asking
    // CreateViewObject for an IContextMenu directly -- is what Explorer itself does to build the
    // folder's background popup. Functionally the two are interchangeable for every verb observed
    // so far (the real fix for registry-registered "Open with VS Code"/"Open in Zed"-style
    // commands misfiring is dispatchChosenId's lpDirectoryW, below), but this is still the
    // documented/correct call for "the background of this folder", so it's kept as the more
    // future-proof choice over the undocumented direct-IContextMenu shortcut.
    IShellView* shellView = nullptr;
    if (FAILED(guardedCreateViewObject(scope.targetFolder, ownerHwnd, IID_IShellView, reinterpret_cast<void**>(&shellView)))
        || !shellView)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to build the folder's background view"));
    }

    const HRESULT getItemObjectResult =
        guardedGetItemObject(shellView, SVGIO_BACKGROUND, IID_IContextMenu, reinterpret_cast<void**>(&scope.contextMenu));
    shellView->Release();

    if (FAILED(getItemObjectResult) || !scope.contextMenu)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to build the folder's background context menu"));
    }

    scope.menu = CreatePopupMenu();
    if (!scope.menu)
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to create the popup menu"));
    }

    if (FAILED(guardedQueryContextMenu(scope.contextMenu, scope.menu, 0, kShellIdFirst, kShellIdLast, CMF_NORMAL | CMF_EXPLORE)))
    {
        return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Failed to query the folder's background context menu"));
    }

    suppressDuplicatedVerbs(scope.menu, scope.contextMenu);
    mergeNativeLayout(scope.menu, layout);

    Logging::debug("ShellContextMenuProvider: background context menu shown");
    const UINT chosenId = trackMenu(scope.menu, scope.contextMenu, ownerHwnd, screenPos);
    return dispatchChosenId(chosenId, scope.contextMenu, ownerHwnd, screenPos, folder);
}

#else

Result<ContextMenuResult> ShellContextMenuProvider::showItemMenu(const std::vector<fs::path>&, const NativeMenuLayout&,
                                                                    NativeWindowHandle, NativeMenuPoint)
{
    return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

Result<ContextMenuResult> ShellContextMenuProvider::showBackgroundMenu(const fs::path&, const NativeMenuLayout&, NativeWindowHandle,
                                                                          NativeMenuPoint)
{
    return Result<ContextMenuResult>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

#endif
