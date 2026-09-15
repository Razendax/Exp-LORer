#include "ShellContextMenuProvider.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

#ifdef _WIN32
#include <Windows.h>

#include <Shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#endif

namespace
{
    namespace fs = std::filesystem;

#ifdef _WIN32
    ContextMenuEntry makeSeparator()
    {
        ContextMenuEntry entry;
        entry.isSeparator = true;
        return entry;
    }

    std::string utf8FromWide(const std::wstring& wide)
    {
        if (wide.empty())
        {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    std::wstring toLowerExtensionOf(const fs::path& path)
    {
        std::wstring ext = path.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return ext;
    }

    std::wstring titleCaseVerb(const std::wstring& verbName)
    {
        if (verbName.empty())
        {
            return verbName;
        }
        std::wstring result = verbName;
        result[0] = static_cast<wchar_t>(towupper(result[0]));
        return result;
    }

    // ---- Registry helpers -------------------------------------------------------------------

    std::optional<std::wstring> readStringValue(HKEY key, const wchar_t* valueName)
    {
        DWORD size = 0;
        LONG rc = RegGetValueW(key, nullptr, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &size);
        if (rc != ERROR_SUCCESS || size == 0)
        {
            return std::nullopt;
        }

        std::wstring value(size / sizeof(wchar_t), L'\0');
        rc = RegGetValueW(key, nullptr, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value.data(), &size);
        if (rc != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        while (!value.empty() && value.back() == L'\0')
        {
            value.pop_back();
        }
        return value;
    }

    bool hasValue(HKEY key, const wchar_t* valueName)
    {
        return RegQueryValueExW(key, valueName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }

    std::vector<std::wstring> enumerateSubkeyNames(HKEY key)
    {
        std::vector<std::wstring> names;
        for (DWORD index = 0;; ++index)
        {
            wchar_t nameBuf[256];
            DWORD nameLen = static_cast<DWORD>(std::size(nameBuf));
            const LONG rc = RegEnumKeyExW(key, index, nameBuf, &nameLen, nullptr, nullptr, nullptr, nullptr);
            if (rc == ERROR_NO_MORE_ITEMS)
            {
                break;
            }
            if (rc != ERROR_SUCCESS)
            {
                break;
            }
            names.emplace_back(nameBuf, nameLen);
        }
        return names;
    }

    std::vector<std::wstring> enumerateValueNames(HKEY key)
    {
        std::vector<std::wstring> names;
        for (DWORD index = 0;; ++index)
        {
            wchar_t nameBuf[256];
            DWORD nameLen = static_cast<DWORD>(std::size(nameBuf));
            const LONG rc = RegEnumValueW(key, index, nameBuf, &nameLen, nullptr, nullptr, nullptr, nullptr);
            if (rc == ERROR_NO_MORE_ITEMS)
            {
                break;
            }
            if (rc != ERROR_SUCCESS)
            {
                break;
            }
            names.emplace_back(nameBuf, nameLen);
        }
        return names;
    }

    std::optional<std::wstring> readHkcrStringValue(const std::wstring& subKeyPath, const wchar_t* valueName)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, subKeyPath.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return std::nullopt;
        }
        auto value = readStringValue(key, valueName);
        RegCloseKey(key);
        return value;
    }

    std::optional<std::wstring> readHkcrDefaultValue(const std::wstring& subKeyPath)
    {
        return readHkcrStringValue(subKeyPath, nullptr);
    }

    // ---- Label / icon resolution -------------------------------------------------------------

    std::wstring resolveIndirectString(const std::wstring& reference)
    {
        wchar_t buffer[1024] = {};
        if (SUCCEEDED(SHLoadIndirectString(reference.c_str(), buffer, static_cast<UINT>(std::size(buffer)), nullptr)))
        {
            return buffer;
        }
        return {};
    }

    std::wstring resolveIndirectOrLiteral(const std::wstring& text)
    {
        if (!text.empty() && text.front() == L'@')
        {
            const std::wstring resolved = resolveIndirectString(text);
            if (!resolved.empty())
            {
                return resolved;
            }
        }
        return text;
    }

    std::wstring resolveVerbLabel(HKEY verbKey, const std::wstring& verbName)
    {
        if (auto muiVerb = readStringValue(verbKey, L"MUIVerb"); muiVerb && !muiVerb->empty())
        {
            const std::wstring resolved = resolveIndirectOrLiteral(*muiVerb);
            if (!resolved.empty())
            {
                return resolved;
            }
        }
        if (auto def = readStringValue(verbKey, nullptr); def && !def->empty())
        {
            return *def;
        }
        return titleCaseVerb(verbName);
    }

    std::wstring extractExecutableIconRefFromCommand(const std::wstring& commandTemplate)
    {
        if (commandTemplate.empty())
        {
            return {};
        }
        std::wstring exe;
        if (commandTemplate.front() == L'"')
        {
            const auto endQuote = commandTemplate.find(L'"', 1);
            exe = endQuote == std::wstring::npos ? commandTemplate.substr(1) : commandTemplate.substr(1, endQuote - 1);
        }
        else
        {
            const auto spacePos = commandTemplate.find(L' ');
            exe = spacePos == std::wstring::npos ? commandTemplate : commandTemplate.substr(0, spacePos);
        }
        if (exe.empty())
        {
            return {};
        }
        return exe + L",0";
    }

    ContextMenuIcon iconFromHBitmap(HBITMAP bitmap)
    {
        ContextMenuIcon icon;
        if (!bitmap)
        {
            return icon;
        }

        BITMAP bmp{};
        if (GetObjectW(bitmap, sizeof(bmp), &bmp) == 0)
        {
            return icon;
        }

        const int width = bmp.bmWidth;
        const int height = bmp.bmHeight;
        if (width <= 0 || height <= 0)
        {
            return icon;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // request top-down rows
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        HDC screenDc = GetDC(nullptr);
        const int scanLines = GetDIBits(screenDc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &bmi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, screenDc);

        if (scanLines == 0)
        {
            return icon;
        }

        // GetDIBits with BI_RGB fills BGRA; swap to RGBA for QImage::Format_RGBA8888 on the UI side.
        for (std::size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            std::swap(pixels[i], pixels[i + 2]);
        }

        icon.width = width;
        icon.height = height;
        icon.rgba = std::move(pixels);
        return icon;
    }

    ContextMenuIcon iconFromHIcon(HICON hIcon)
    {
        ContextMenuIcon icon;
        if (!hIcon)
        {
            return icon;
        }

        ICONINFO info{};
        if (!GetIconInfo(hIcon, &info))
        {
            return icon;
        }

        icon = iconFromHBitmap(info.hbmColor);

        if (info.hbmColor)
        {
            DeleteObject(info.hbmColor);
        }
        if (info.hbmMask)
        {
            DeleteObject(info.hbmMask);
        }
        return icon;
    }

    // iconRef is "<path>,<index>" (index may be negative for a resource id) or a bare path.
    ContextMenuIcon extractIconForReference(const std::wstring& iconRef)
    {
        if (iconRef.empty())
        {
            return {};
        }

        std::wstring path = iconRef;
        int index = 0;
        const auto commaPos = iconRef.find_last_of(L',');
        if (commaPos != std::wstring::npos)
        {
            path = iconRef.substr(0, commaPos);
            try
            {
                index = std::stoi(iconRef.substr(commaPos + 1));
            }
            catch (...)
            {
                index = 0;
            }
        }
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
        {
            path = path.substr(1, path.size() - 2);
        }
        if (path.empty())
        {
            return {};
        }

        HICON hIcon = nullptr;
        const UINT extracted = ExtractIconExW(path.c_str(), index, nullptr, &hIcon, 1);
        if (extracted == 0 || !hIcon)
        {
            return {};
        }

        ContextMenuIcon icon = iconFromHIcon(hIcon);
        DestroyIcon(hIcon);
        return icon;
    }

    // ---- Static verb enumeration ------------------------------------------------------------

    const std::set<std::wstring>& suppressedVerbNames()
    {
        // Cut/Copy/Paste/Delete/Rename/Properties/default-Open have app-native equivalents that
        // the UI layer places at fixed positions instead (Architecture.md §14.13).
        static const std::set<std::wstring> names = { L"open", L"cut", L"copy", L"paste", L"delete", L"rename", L"properties" };
        return names;
    }

    void collectVerbsFromShellKey(HKEY shellKey, const std::vector<fs::path>& targetPaths, std::vector<ContextMenuEntry>& outEntries,
                                   std::map<std::uint32_t, ShellContextMenuProvider::StaticCommand>& outCommands, std::uint32_t& nextId)
    {
        for (const std::wstring& verbName : enumerateSubkeyNames(shellKey))
        {
            std::wstring lowerVerb = verbName;
            std::transform(lowerVerb.begin(), lowerVerb.end(), lowerVerb.begin(),
                            [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            if (suppressedVerbNames().count(lowerVerb) != 0)
            {
                continue;
            }

            HKEY verbKey = nullptr;
            if (RegOpenKeyExW(shellKey, verbName.c_str(), 0, KEY_READ, &verbKey) != ERROR_SUCCESS)
            {
                continue;
            }
            if (hasValue(verbKey, L"LegacyDisable") || hasValue(verbKey, L"ProgrammaticAccessOnly")
                || hasValue(verbKey, L"Extended"))
            {
                RegCloseKey(verbKey);
                continue;
            }

            ContextMenuEntry entry;
            entry.label = utf8FromWide(resolveVerbLabel(verbKey, verbName));
            entry.id = nextId++;

            std::wstring iconRef = readStringValue(verbKey, L"Icon").value_or(std::wstring());

            HKEY subShellKey = nullptr;
            bool isSubmenu = false;
            if (RegOpenKeyExW(verbKey, L"shell", 0, KEY_READ, &subShellKey) == ERROR_SUCCESS)
            {
                isSubmenu = true;
                collectVerbsFromShellKey(subShellKey, targetPaths, entry.submenu, outCommands, nextId);
                RegCloseKey(subShellKey);
            }

            bool hasCommand = false;
            if (!isSubmenu)
            {
                HKEY commandKey = nullptr;
                if (RegOpenKeyExW(verbKey, L"command", 0, KEY_READ, &commandKey) == ERROR_SUCCESS)
                {
                    std::wstring commandTemplate = readStringValue(commandKey, nullptr).value_or(std::wstring());
                    RegCloseKey(commandKey);
                    if (!commandTemplate.empty())
                    {
                        hasCommand = true;
                        if (iconRef.empty())
                        {
                            iconRef = extractExecutableIconRefFromCommand(commandTemplate);
                        }
                        outCommands[entry.id] = ShellContextMenuProvider::StaticCommand{ commandTemplate, targetPaths };
                    }
                }
            }
            RegCloseKey(verbKey);

            if (!isSubmenu && !hasCommand)
            {
                // Nothing to invoke and no submenu — not a usable entry (e.g. a DDEExec-only verb).
                continue;
            }

            if (!iconRef.empty())
            {
                entry.icon = extractIconForReference(iconRef);
            }

            outEntries.push_back(std::move(entry));
        }
    }

    void collectStaticVerbsForRoot(const std::wstring& base, const std::vector<fs::path>& targetPaths,
                                    std::vector<ContextMenuEntry>& outEntries,
                                    std::map<std::uint32_t, ShellContextMenuProvider::StaticCommand>& outCommands,
                                    std::uint32_t& nextId)
    {
        HKEY shellKey = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (base + L"\\shell").c_str(), 0, KEY_READ, &shellKey) != ERROR_SUCCESS)
        {
            return;
        }
        collectVerbsFromShellKey(shellKey, targetPaths, outEntries, outCommands, nextId);
        RegCloseKey(shellKey);
    }

    // ---- "Open with" submenu -----------------------------------------------------------------

    void appendOpenWithAppEntry(const std::wstring& exeName, const std::vector<fs::path>& targetPaths,
                                 std::vector<ContextMenuEntry>& appEntries,
                                 std::map<std::uint32_t, ShellContextMenuProvider::StaticCommand>& outCommands, std::uint32_t& nextId)
    {
        const std::wstring applicationsKey = L"Applications\\" + exeName;

        std::wstring friendlyName = readHkcrStringValue(applicationsKey, L"FriendlyAppName").value_or(std::wstring());
        friendlyName = resolveIndirectOrLiteral(friendlyName);
        if (friendlyName.empty())
        {
            friendlyName = exeName;
        }

        const std::wstring commandTemplate =
            readHkcrStringValue(applicationsKey + L"\\shell\\open\\command", nullptr).value_or(L"\"" + exeName + L"\" \"%1\"");

        ContextMenuEntry entry;
        entry.label = utf8FromWide(friendlyName);
        entry.id = nextId++;

        const std::wstring iconRef = readHkcrStringValue(applicationsKey, L"DefaultIcon").value_or(exeName + L",0");
        entry.icon = extractIconForReference(iconRef);

        outCommands[entry.id] = ShellContextMenuProvider::StaticCommand{ commandTemplate, targetPaths };
        appEntries.push_back(std::move(entry));
    }

    void collectOpenWithSubmenu(const std::wstring& ext, const std::vector<fs::path>& targetPaths,
                                 std::vector<ContextMenuEntry>& outEntries,
                                 std::map<std::uint32_t, ShellContextMenuProvider::StaticCommand>& outCommands, std::uint32_t& nextId)
    {
        std::vector<ContextMenuEntry> appEntries;
        std::set<std::wstring> seenExe;

        HKEY progIdsKey = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (ext + L"\\OpenWithProgIds").c_str(), 0, KEY_READ, &progIdsKey) == ERROR_SUCCESS)
        {
            for (const std::wstring& progId : enumerateValueNames(progIdsKey))
            {
                const std::wstring commandTemplate = readHkcrStringValue(progId + L"\\shell\\open\\command", nullptr).value_or(std::wstring());
                if (commandTemplate.empty())
                {
                    continue;
                }
                std::wstring friendlyName = resolveIndirectOrLiteral(readHkcrStringValue(progId, L"FriendlyTypeName").value_or(std::wstring()));
                if (friendlyName.empty())
                {
                    friendlyName = readHkcrDefaultValue(progId).value_or(progId);
                }

                ContextMenuEntry entry;
                entry.label = utf8FromWide(friendlyName);
                entry.id = nextId++;
                outCommands[entry.id] = ShellContextMenuProvider::StaticCommand{ commandTemplate, targetPaths };
                appEntries.push_back(std::move(entry));
            }
            RegCloseKey(progIdsKey);
        }

        HKEY listKey = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (ext + L"\\OpenWithList").c_str(), 0, KEY_READ, &listKey) == ERROR_SUCCESS)
        {
            for (const std::wstring& exeName : enumerateSubkeyNames(listKey))
            {
                if (seenExe.insert(exeName).second)
                {
                    appendOpenWithAppEntry(exeName, targetPaths, appEntries, outCommands, nextId);
                }
            }
            RegCloseKey(listKey);
        }

        HKEY mruKey = nullptr;
        const std::wstring mruPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\" + ext + L"\\OpenWithList";
        if (RegOpenKeyExW(HKEY_CURRENT_USER, mruPath.c_str(), 0, KEY_READ, &mruKey) == ERROR_SUCCESS)
        {
            for (const std::wstring& valueName : enumerateValueNames(mruKey))
            {
                if (valueName == L"MRUList")
                {
                    continue;
                }
                auto exeName = readStringValue(mruKey, valueName.c_str());
                if (exeName && !exeName->empty() && seenExe.insert(*exeName).second)
                {
                    appendOpenWithAppEntry(*exeName, targetPaths, appEntries, outCommands, nextId);
                }
            }
            RegCloseKey(mruKey);
        }

        if (!appEntries.empty())
        {
            ContextMenuEntry submenu;
            submenu.label = "Open with";
            submenu.id = nextId++;
            submenu.submenu = std::move(appEntries);
            outEntries.push_back(std::move(submenu));
        }
    }

    // ---- "New" submenu (background only) -----------------------------------------------------

    struct ShellNewTypeInfo
    {
        std::wstring label;
        std::wstring newBaseName;
        std::wstring extension;
        std::optional<fs::path> templateFile;
        std::wstring iconRef;
    };

    std::vector<ShellNewTypeInfo> buildShellNewTypeCache()
    {
        std::vector<ShellNewTypeInfo> result;

        for (DWORD index = 0;; ++index)
        {
            wchar_t nameBuf[256];
            DWORD nameLen = static_cast<DWORD>(std::size(nameBuf));
            const LONG rc = RegEnumKeyExW(HKEY_CLASSES_ROOT, index, nameBuf, &nameLen, nullptr, nullptr, nullptr, nullptr);
            if (rc == ERROR_NO_MORE_ITEMS)
            {
                break;
            }
            if (rc != ERROR_SUCCESS)
            {
                continue;
            }

            const std::wstring ext(nameBuf, nameLen);
            if (ext.empty() || ext.front() != L'.')
            {
                continue;
            }

            HKEY shellNewKey = nullptr;
            if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (ext + L"\\ShellNew").c_str(), 0, KEY_READ, &shellNewKey) != ERROR_SUCCESS)
            {
                continue;
            }
            const bool isNullFile = hasValue(shellNewKey, L"NullFile");
            const auto fileName = readStringValue(shellNewKey, L"FileName");
            RegCloseKey(shellNewKey);

            if (!isNullFile && !fileName)
            {
                // ShellNew's Command/binary-Data mechanisms are out of scope (Architecture.md §14.13).
                continue;
            }

            std::wstring label = ext;
            if (auto progId = readHkcrDefaultValue(ext); progId && !progId->empty())
            {
                if (auto friendly = readHkcrDefaultValue(*progId); friendly && !friendly->empty())
                {
                    label = *friendly;
                }
            }

            ShellNewTypeInfo info;
            info.extension = ext;
            info.label = label;
            info.newBaseName = L"New " + label;

            if (fileName && !fileName->empty())
            {
                fs::path templatePath(*fileName);
                if (templatePath.is_relative())
                {
                    wchar_t windowsDir[MAX_PATH];
                    if (GetWindowsDirectoryW(windowsDir, static_cast<UINT>(std::size(windowsDir))) != 0)
                    {
                        templatePath = fs::path(windowsDir) / L"ShellNew" / templatePath;
                    }
                }
                info.templateFile = templatePath;
            }

            auto iconRef = readHkcrStringValue(ext, L"DefaultIcon");
            if (!iconRef)
            {
                if (auto progId = readHkcrDefaultValue(ext); progId && !progId->empty())
                {
                    iconRef = readHkcrStringValue(*progId, L"DefaultIcon");
                }
            }
            info.iconRef = iconRef.value_or(std::wstring());

            result.push_back(std::move(info));
        }

        return result;
    }

    // Registry-sourced ShellNew entries only. "New Folder" itself is a native QAction the UI
    // layer places ahead of this submenu (FileOperationsController::createFolder), not duplicated
    // here — see Architecture.md §14.13's IFileSystemRepository::createDirectory addition.
    void collectShellNewSubmenu(const fs::path& backgroundFolder, std::vector<ContextMenuEntry>& outEntries,
                                 std::map<std::uint32_t, ShellContextMenuProvider::ShellNewCommand>& outCommands, std::uint32_t& nextId)
    {
        static const std::vector<ShellNewTypeInfo> cachedTypes = buildShellNewTypeCache();
        if (cachedTypes.empty())
        {
            return;
        }

        std::vector<ContextMenuEntry> typeEntries;
        for (const ShellNewTypeInfo& info : cachedTypes)
        {
            ContextMenuEntry entry;
            entry.label = utf8FromWide(info.label);
            entry.id = nextId++;
            if (!info.iconRef.empty())
            {
                entry.icon = extractIconForReference(info.iconRef);
            }

            ShellContextMenuProvider::ShellNewCommand command;
            command.targetDirectory = backgroundFolder;
            command.templateFile = info.templateFile;
            command.baseName = info.newBaseName;
            command.extension = info.extension;
            outCommands[entry.id] = std::move(command);

            typeEntries.push_back(std::move(entry));
        }

        ContextMenuEntry newSubmenu;
        newSubmenu.label = "New";
        newSubmenu.id = nextId++;
        newSubmenu.submenu = std::move(typeEntries);
        outEntries.push_back(std::move(newSubmenu));
    }

    // ---- Dynamic shell-extension (COM) resolution ---------------------------------------------

    void walkDynamicMenu(HMENU menu, UINT idFirst, IContextMenu* handler, std::vector<ContextMenuEntry>& outEntries,
                          std::map<std::uint32_t, ShellContextMenuProvider::DynamicCommand>& outCommands, std::uint32_t& nextId)
    {
        const int count = GetMenuItemCount(menu);
        for (int i = 0; i < count; ++i)
        {
            wchar_t textBuf[512] = {};
            MENUITEMINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_BITMAP;
            info.dwTypeData = textBuf;
            info.cch = static_cast<UINT>(std::size(textBuf));
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info))
            {
                continue;
            }

            if (info.fType & MFT_SEPARATOR)
            {
                outEntries.push_back(makeSeparator());
                continue;
            }

            ContextMenuEntry entry;
            entry.label = utf8FromWide(textBuf);
            entry.enabled = (info.fState & MFS_DISABLED) == 0;
            if (info.hbmpItem)
            {
                entry.icon = iconFromHBitmap(info.hbmpItem);
            }

            if (info.hSubMenu)
            {
                walkDynamicMenu(info.hSubMenu, idFirst, handler, entry.submenu, outCommands, nextId);
                entry.id = nextId++;
            }
            else if (info.wID != 0)
            {
                entry.id = nextId++;
                outCommands[entry.id] = ShellContextMenuProvider::DynamicCommand{ handler, info.wID - idFirst };
            }
            else
            {
                continue;
            }

            outEntries.push_back(std::move(entry));
        }
    }

    std::optional<CLSID> resolveClsid(const std::wstring& text)
    {
        CLSID clsid{};
        if (SUCCEEDED(CLSIDFromString(text.c_str(), &clsid)))
        {
            return clsid;
        }
        if (SUCCEEDED(CLSIDFromProgID(text.c_str(), &clsid)))
        {
            return clsid;
        }
        return std::nullopt;
    }

    // Enumerates shellex\ContextMenuHandlers under HKCR\<base>, initializes each handler, and
    // queries its menu content into a per-handler id subrange (handler N -> [N*1000, N*1000+999),
    // mirroring how Explorer offsets each handler's idCmdFirst). Contributing handlers are kept
    // alive in pendingHandlers (released by ShellContextMenuProvider::discardMenu()).
    void collectHandlersFromRoot(const std::wstring& base, PCIDLIST_ABSOLUTE pidlFolder, IDataObject* dataObject,
                                  std::vector<ContextMenuEntry>& outEntries, int& handlerIndex, std::set<std::wstring>& seenClsid,
                                  std::map<std::uint32_t, ShellContextMenuProvider::DynamicCommand>& outCommands,
                                  std::uint32_t& nextId, std::vector<IContextMenu*>& pendingHandlers, bool isBackground)
    {
        HKEY shellexKey = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (base + L"\\shellex\\ContextMenuHandlers").c_str(), 0, KEY_READ, &shellexKey)
            != ERROR_SUCCESS)
        {
            return;
        }

        for (const std::wstring& subkeyName : enumerateSubkeyNames(shellexKey))
        {
            HKEY subkey = nullptr;
            if (RegOpenKeyExW(shellexKey, subkeyName.c_str(), 0, KEY_READ, &subkey) != ERROR_SUCCESS)
            {
                continue;
            }
            std::wstring clsidText = readStringValue(subkey, nullptr).value_or(subkeyName);
            RegCloseKey(subkey);

            if (clsidText.empty() || !seenClsid.insert(clsidText).second)
            {
                continue;
            }

            const auto clsid = resolveClsid(clsidText);
            if (!clsid)
            {
                continue;
            }

            IShellExtInit* shellExtInit = nullptr;
            if (FAILED(CoCreateInstance(*clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IShellExtInit,
                                         reinterpret_cast<void**>(&shellExtInit)))
                || !shellExtInit)
            {
                continue;
            }

            if (FAILED(shellExtInit->Initialize(pidlFolder, dataObject, nullptr)))
            {
                shellExtInit->Release();
                continue;
            }

            IContextMenu* contextMenu = nullptr;
            const HRESULT queryResult = shellExtInit->QueryInterface(IID_IContextMenu, reinterpret_cast<void**>(&contextMenu));
            shellExtInit->Release();
            if (FAILED(queryResult) || !contextMenu)
            {
                continue;
            }

            const UINT idFirst = static_cast<UINT>(handlerIndex * 1000 + 1);
            const UINT idLast = idFirst + 998;
            HMENU scratch = CreatePopupMenu();
            if (!scratch)
            {
                contextMenu->Release();
                continue;
            }

            const UINT flags = CMF_NORMAL | (isBackground ? static_cast<UINT>(CMF_EXPLORE) : 0u);
            const HRESULT queryMenuResult = contextMenu->QueryContextMenu(scratch, 0, idFirst, idLast, flags);
            ++handlerIndex;

            if (FAILED(queryMenuResult) || GetMenuItemCount(scratch) <= 0)
            {
                DestroyMenu(scratch);
                contextMenu->Release();
                continue;
            }

            std::vector<ContextMenuEntry> handlerEntries;
            walkDynamicMenu(scratch, idFirst, contextMenu, handlerEntries, outCommands, nextId);
            DestroyMenu(scratch);

            if (handlerEntries.empty())
            {
                contextMenu->Release();
                continue;
            }

            outEntries.insert(outEntries.end(), std::make_move_iterator(handlerEntries.begin()),
                               std::make_move_iterator(handlerEntries.end()));
            pendingHandlers.push_back(contextMenu);
        }

        RegCloseKey(shellexKey);
    }

    // ---- Command-line expansion / launch (invoke-time only) ------------------------------------

    std::wstring expandCommandArguments(const std::wstring& commandTemplate, const std::vector<fs::path>& targetPaths)
    {
        if (targetPaths.empty())
        {
            return commandTemplate;
        }

        std::wstring result;
        result.reserve(commandTemplate.size());
        bool substituted = false;

        for (std::size_t i = 0; i < commandTemplate.size(); ++i)
        {
            const wchar_t c = commandTemplate[i];
            if (c == L'%' && i + 1 < commandTemplate.size())
            {
                const wchar_t next = commandTemplate[i + 1];
                if (next == L'1' || next == L'L' || next == L'l')
                {
                    result += L'"' + targetPaths.front().wstring() + L'"';
                    ++i;
                    substituted = true;
                    continue;
                }
                if (next == L'*')
                {
                    for (std::size_t j = 0; j < targetPaths.size(); ++j)
                    {
                        if (j != 0)
                        {
                            result += L' ';
                        }
                        result += L'"' + targetPaths[j].wstring() + L'"';
                    }
                    ++i;
                    substituted = true;
                    continue;
                }
            }
            result += c;
        }

        if (!substituted)
        {
            result += L" \"" + targetPaths.front().wstring() + L'"';
        }
        return result;
    }

    Result<void> launchCommandLine(std::wstring commandLine, NativeWindowHandle ownerWindow, const fs::path& fallbackTarget)
    {
        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};

        commandLine.push_back(L'\0');
        const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                                             &startupInfo, &processInfo);
        if (created)
        {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            return Result<void>::success();
        }

        // Fall back to ShellExecuteW on the target itself for command templates that are bare
        // verbs/documents rather than a directly-launchable executable.
        const HINSTANCE result =
            ShellExecuteW(static_cast<HWND>(ownerWindow), nullptr, fallbackTarget.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            return Result<void>::failure(Error(ErrorCode::IoError, "Failed to invoke the context menu command"));
        }
        return Result<void>::success();
    }
#endif
}

#ifdef _WIN32

ShellContextMenuProvider::ShellContextMenuProvider() = default;

ShellContextMenuProvider::~ShellContextMenuProvider()
{
    discardMenu();
}

Result<std::vector<ContextMenuEntry>> ShellContextMenuProvider::buildItemMenu(const std::vector<fs::path>& paths,
                                                                                ContextMenuSourceMode mode)
{
    discardMenu();

    if (paths.empty())
    {
        return Result<std::vector<ContextMenuEntry>>::failure(Error(ErrorCode::InvalidArgument, "No items to build a context menu for"));
    }

    const fs::path parentDirectory = paths.front().parent_path();
    for (const auto& path : paths)
    {
        if (path.parent_path() != parentDirectory)
        {
            return Result<std::vector<ContextMenuEntry>>::failure(
                Error(ErrorCode::InvalidArgument, "All items must share one parent directory"));
        }
    }

    std::error_code ec;
    const bool isDirectorySelection = fs::is_directory(paths.front(), ec);

    std::vector<std::wstring> rootBases;
    std::wstring extLower;
    if (isDirectorySelection)
    {
        rootBases = { L"AllFilesystemObjects", L"Directory", L"Folder" };
    }
    else
    {
        extLower = toLowerExtensionOf(paths.front());
        if (!extLower.empty())
        {
            if (auto progId = readHkcrDefaultValue(extLower); progId && !progId->empty())
            {
                rootBases.push_back(*progId);
            }
            rootBases.push_back(extLower);
            rootBases.push_back(L"SystemFileAssociations\\" + extLower);
            if (auto perceivedType = readHkcrStringValue(extLower, L"PerceivedType"); perceivedType && !perceivedType->empty())
            {
                rootBases.push_back(L"SystemFileAssociations\\" + *perceivedType);
            }
        }
        rootBases.push_back(L"*");
        rootBases.push_back(L"AllFilesystemObjects");
    }

    std::vector<ContextMenuEntry> entries;
    for (const std::wstring& base : rootBases)
    {
        collectStaticVerbsForRoot(base, paths, entries, m_staticCommands, m_nextEntryId);
    }

    if (!isDirectorySelection && !extLower.empty())
    {
        collectOpenWithSubmenu(extLower, paths, entries, m_staticCommands, m_nextEntryId);
    }

    if (mode == ContextMenuSourceMode::StaticAndShellExtensions)
    {
        IShellFolder* desktop = nullptr;
        if (SUCCEEDED(SHGetDesktopFolder(&desktop)) && desktop)
        {
            PIDLIST_ABSOLUTE parentPidl = nullptr;
            if (SUCCEEDED(SHParseDisplayName(parentDirectory.c_str(), nullptr, &parentPidl, 0, nullptr)) && parentPidl)
            {
                IShellFolder* parentFolder = nullptr;
                if (SUCCEEDED(desktop->BindToObject(parentPidl, nullptr, IID_IShellFolder, reinterpret_cast<void**>(&parentFolder)))
                    && parentFolder)
                {
                    std::vector<PITEMID_CHILD> childPidls;
                    for (const auto& path : paths)
                    {
                        std::wstring name = path.filename().wstring();
                        PIDLIST_RELATIVE childPidl = nullptr;
                        if (SUCCEEDED(parentFolder->ParseDisplayName(nullptr, nullptr, name.data(), nullptr, &childPidl, nullptr))
                            && childPidl)
                        {
                            childPidls.push_back(reinterpret_cast<PITEMID_CHILD>(childPidl));
                        }
                    }

                    if (!childPidls.empty())
                    {
                        std::vector<LPCITEMIDLIST> constChildPidls(childPidls.begin(), childPidls.end());
                        IDataObject* dataObject = nullptr;
                        parentFolder->GetUIObjectOf(nullptr, static_cast<UINT>(constChildPidls.size()), constChildPidls.data(),
                                                     IID_IDataObject, nullptr, reinterpret_cast<void**>(&dataObject));

                        std::vector<ContextMenuEntry> dynamicEntries;
                        int handlerIndex = 0;
                        std::set<std::wstring> seenClsid;
                        for (const std::wstring& base : rootBases)
                        {
                            collectHandlersFromRoot(base, parentPidl, dataObject, dynamicEntries, handlerIndex, seenClsid,
                                                     m_dynamicCommands, m_nextEntryId, m_pendingHandlers, false);
                        }

                        if (dataObject)
                        {
                            dataObject->Release();
                        }
                        if (!dynamicEntries.empty())
                        {
                            entries.push_back(makeSeparator());
                            entries.insert(entries.end(), std::make_move_iterator(dynamicEntries.begin()),
                                            std::make_move_iterator(dynamicEntries.end()));
                        }
                    }

                    for (auto* pidl : childPidls)
                    {
                        CoTaskMemFree(pidl);
                    }
                    parentFolder->Release();
                }
                CoTaskMemFree(parentPidl);
            }
            desktop->Release();
        }
    }

    return Result<std::vector<ContextMenuEntry>>::success(std::move(entries));
}

Result<std::vector<ContextMenuEntry>> ShellContextMenuProvider::buildBackgroundMenu(const fs::path& folder, ContextMenuSourceMode mode)
{
    discardMenu();

    const std::vector<std::wstring> rootBases = { L"Directory\\Background" };

    std::vector<ContextMenuEntry> entries;
    for (const std::wstring& base : rootBases)
    {
        collectStaticVerbsForRoot(base, {}, entries, m_staticCommands, m_nextEntryId);
    }

    collectShellNewSubmenu(folder, entries, m_shellNewCommands, m_nextEntryId);

    if (mode == ContextMenuSourceMode::StaticAndShellExtensions)
    {
        PIDLIST_ABSOLUTE folderPidl = nullptr;
        if (SUCCEEDED(SHParseDisplayName(folder.c_str(), nullptr, &folderPidl, 0, nullptr)) && folderPidl)
        {
            std::vector<ContextMenuEntry> dynamicEntries;
            int handlerIndex = 0;
            std::set<std::wstring> seenClsid;
            for (const std::wstring& base : rootBases)
            {
                collectHandlersFromRoot(base, folderPidl, nullptr, dynamicEntries, handlerIndex, seenClsid, m_dynamicCommands,
                                         m_nextEntryId, m_pendingHandlers, true);
            }

            if (!dynamicEntries.empty())
            {
                entries.push_back(makeSeparator());
                entries.insert(entries.end(), std::make_move_iterator(dynamicEntries.begin()), std::make_move_iterator(dynamicEntries.end()));
            }

            CoTaskMemFree(folderPidl);
        }
    }

    return Result<std::vector<ContextMenuEntry>>::success(std::move(entries));
}

Result<void> ShellContextMenuProvider::invoke(std::uint32_t entryId, NativeWindowHandle ownerWindow)
{
    if (auto it = m_staticCommands.find(entryId); it != m_staticCommands.end())
    {
        const StaticCommand& command = it->second;
        const std::wstring expanded = expandCommandArguments(command.commandTemplate, command.targetPaths);
        const fs::path fallbackTarget = command.targetPaths.empty() ? fs::path() : command.targetPaths.front();
        return launchCommandLine(expanded, ownerWindow, fallbackTarget);
    }

    if (auto it = m_dynamicCommands.find(entryId); it != m_dynamicCommands.end())
    {
        const DynamicCommand& command = it->second;
        CMINVOKECOMMANDINFOEX info{};
        info.cbSize = sizeof(info);
        info.fMask = CMIC_MASK_UNICODE;
        info.hwnd = static_cast<HWND>(ownerWindow);
        info.lpVerb = MAKEINTRESOURCEA(command.commandOffset);
        info.lpVerbW = MAKEINTRESOURCEW(command.commandOffset);
        info.nShow = SW_SHOWNORMAL;

        const HRESULT hr = command.handler->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
        if (FAILED(hr))
        {
            return Result<void>::failure(Error(ErrorCode::IoError, "Failed to invoke the shell extension command"));
        }
        return Result<void>::success();
    }

    if (auto it = m_shellNewCommands.find(entryId); it != m_shellNewCommands.end())
    {
        const ShellNewCommand& command = it->second;

        std::error_code ec;
        int counter = 2;
        fs::path candidate = command.targetDirectory / (command.baseName + command.extension);
        while (fs::exists(candidate, ec))
        {
            candidate = command.targetDirectory / (command.baseName + L" (" + std::to_wstring(counter) + L")" + command.extension);
            ++counter;
        }

        try
        {
            if (command.templateFile)
            {
                fs::copy_file(*command.templateFile, candidate);
            }
            else
            {
                std::ofstream stream(candidate, std::ios::binary);
                if (!stream)
                {
                    return Result<void>::failure(Error(ErrorCode::IoError, "Failed to create new file"));
                }
            }
        }
        catch (const fs::filesystem_error& e)
        {
            return Result<void>::failure(Error(ErrorCode::IoError, e.what()));
        }

        return Result<void>::success();
    }

    return Result<void>::failure(Error(ErrorCode::InvalidArgument, "Unknown context menu entry"));
}

void ShellContextMenuProvider::discardMenu()
{
    for (IContextMenu* handler : m_pendingHandlers)
    {
        if (handler)
        {
            handler->Release();
        }
    }
    m_pendingHandlers.clear();
    m_dynamicCommands.clear();
    m_staticCommands.clear();
    m_shellNewCommands.clear();
    m_nextEntryId = 1;
}

#else

ShellContextMenuProvider::ShellContextMenuProvider() = default;
ShellContextMenuProvider::~ShellContextMenuProvider() = default;

Result<std::vector<ContextMenuEntry>> ShellContextMenuProvider::buildItemMenu(const std::vector<fs::path>&, ContextMenuSourceMode)
{
    return Result<std::vector<ContextMenuEntry>>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

Result<std::vector<ContextMenuEntry>> ShellContextMenuProvider::buildBackgroundMenu(const fs::path&, ContextMenuSourceMode)
{
    return Result<std::vector<ContextMenuEntry>>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

Result<void> ShellContextMenuProvider::invoke(std::uint32_t, NativeWindowHandle)
{
    return Result<void>::failure(Error(ErrorCode::IoError, "Not supported on this platform"));
}

void ShellContextMenuProvider::discardMenu()
{
}

#endif
