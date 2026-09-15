#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "IContextMenuProvider.h"

#ifdef _WIN32
struct IContextMenu;
#endif

// Implements IContextMenuProvider by reading Windows Registry verbs (and, optionally, COM
// shell-extension IContextMenu handlers) into plain ContextMenuEntry data — no OS popup is shown;
// the popup itself is a Qt-drawn QMenu built by src/ui/widgets/ContextMenuBuilder from this data
// (Architecture.md §14.13). Named after the StandardFileSystemRepository pattern (platform-general
// name, #ifdef _WIN32 internals) rather than a Windows-specific class name, so CompositionRoot
// wiring doesn't need to change when a Linux implementation eventually lands behind the same Port.
// Must only ever be called from the Qt UI thread (no explicit CoInitializeEx — this assumes COM
// STA is already initialized by Qt's Windows platform plugin, same assumption ShellExecuteW/
// SHFileOperationW elsewhere in the codebase already make).
class ShellContextMenuProvider : public IContextMenuProvider
{
public:
    ShellContextMenuProvider();
    ~ShellContextMenuProvider() override;

    Result<std::vector<ContextMenuEntry>> buildItemMenu(const std::vector<std::filesystem::path>& paths,
                                                          ContextMenuSourceMode mode) override;
    Result<std::vector<ContextMenuEntry>> buildBackgroundMenu(const std::filesystem::path& folder,
                                                                ContextMenuSourceMode mode) override;
    Result<void> invoke(std::uint32_t entryId, NativeWindowHandle ownerWindow) override;
    void discardMenu() override;

#ifdef _WIN32
    // Public only so the free helper functions in ShellContextMenuProvider.cpp can name these
    // types; not part of the Port-facing API (callers only ever see IContextMenuProvider).

    // A static (registry-verb or Open-with) entry's invoke() action: expand this command-line
    // template against targetPaths and launch it.
    struct StaticCommand
    {
        std::wstring commandTemplate;
        std::vector<std::filesystem::path> targetPaths;
    };

    // A dynamic (COM shell-extension) entry's invoke() action: call InvokeCommand on the handler
    // that produced it, with the id offset within that handler's own subrange.
    struct DynamicCommand
    {
        IContextMenu* handler = nullptr;
        unsigned int commandOffset = 0;
    };

    // A registry-ShellNew "New > <type>" entry's invoke() action: create a uniquely-named file in
    // targetDirectory, copying templateFile's bytes if set, else creating it empty.
    struct ShellNewCommand
    {
        std::filesystem::path targetDirectory;
        std::optional<std::filesystem::path> templateFile;
        std::wstring baseName;
        std::wstring extension;
    };
#endif

private:
#ifdef _WIN32
    std::map<std::uint32_t, StaticCommand> m_staticCommands;
    std::map<std::uint32_t, DynamicCommand> m_dynamicCommands;
    std::map<std::uint32_t, ShellNewCommand> m_shellNewCommands;
    std::vector<IContextMenu*> m_pendingHandlers; // one AddRef per entry; released by discardMenu()
    std::uint32_t m_nextEntryId = 1;
#endif
};
