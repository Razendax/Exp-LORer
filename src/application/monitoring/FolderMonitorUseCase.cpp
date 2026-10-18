#include "FolderMonitorUseCase.h"

#include <chrono>
#include <memory>
#include <unordered_map>
#include <utility>

namespace
{
    // ReadDirectoryChangesW commonly reports one logical save as two or more raw notifications for
    // the same path in quick succession (e.g. a content write followed by a last-write-time/
    // attribute update), which otherwise shows up as the same entry twice in the Monitoring log.
    // Collapsing same-path-same-kind repeats within this window turns that into a single entry
    // without needing a timer/Qt dependency in this layer -- a later, different-kind event for the
    // same path (e.g. Removed right after Added) is never suppressed.
    constexpr std::chrono::milliseconds kDebounceWindow{ 500 };
}

FolderMonitorUseCase::FolderMonitorUseCase(IFolderMonitor& folderMonitor)
    : m_folderMonitor(folderMonitor)
{
}

void FolderMonitorUseCase::startMonitoring(const std::filesystem::path& rootDirectory,
                                            std::function<void(const std::vector<FolderChangeEvent>&)> onChanged,
                                            std::function<void(Result<FolderMonitorId>)> onStarted)
{
    // Debounce state is scoped to this one monitoring session (captured by the wrapping lambda
    // below), never shared across tabs/roots, so two unrelated tabs touching same-named relative
    // paths around the same time can't suppress each other's events.
    using LastSeenMap = std::unordered_map<std::wstring, std::pair<FolderChangeKind, std::chrono::steady_clock::time_point>>;
    auto lastSeen = std::make_shared<LastSeenMap>();

    auto debouncedOnChanged = [onChanged = std::move(onChanged), lastSeen](const std::vector<FolderChangeEvent>& batch) {
        const auto now = std::chrono::steady_clock::now();

        // Prune entries whose debounce window has already elapsed before considering this batch --
        // only the last kDebounceWindow of history is ever consulted below, so anything older than
        // that is dead weight. Without this, a long-running monitoring session (e.g. a long build)
        // touching many distinct paths would grow this map for the lifetime of the session.
        for (auto it = lastSeen->begin(); it != lastSeen->end();)
        {
            if (now - it->second.second >= kDebounceWindow)
            {
                it = lastSeen->erase(it);
            }
            else
            {
                ++it;
            }
        }

        std::vector<FolderChangeEvent> filtered;
        filtered.reserve(batch.size());

        for (const FolderChangeEvent& event : batch)
        {
            const std::wstring key = event.relativePath.wstring();
            const auto found = lastSeen->find(key);
            if (found != lastSeen->end() && found->second.first == event.kind && now - found->second.second < kDebounceWindow)
            {
                found->second.second = now;
                continue;
            }

            (*lastSeen)[key] = { event.kind, now };
            filtered.push_back(event);
        }

        if (!filtered.empty())
        {
            onChanged(filtered);
        }
    };

    m_folderMonitor.startMonitoring(rootDirectory, std::move(debouncedOnChanged), std::move(onStarted));
}

void FolderMonitorUseCase::stopMonitoring(FolderMonitorId id)
{
    m_folderMonitor.stopMonitoring(id);
}
