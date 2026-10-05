#include "DuplicateFinderWorker.h"

#include <utility>

#include "IFileSystemRepository.h"

DuplicateFinderWorker::DuplicateFinderWorker(DuplicateFinderUseCase& useCase, IFileSystemRepository& fileSystemRepository,
                                              QObject* parent)
    : QObject(parent)
    , m_useCase(useCase)
    , m_fileSystemRepository(fileSystemRepository)
{
}

Result<std::vector<DuplicateGroupInfo>> DuplicateFinderWorker::runScan(
    const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    const std::shared_ptr<std::atomic<bool>>& cancelFlag,
    const std::function<void(std::size_t hashed, std::size_t totalCandidates)>& onProgress)
{
    auto scanned = m_useCase.scan(root, minFileSizeBytes, onProgress, *cancelFlag);
    if (scanned.hasError())
    {
        return Result<std::vector<DuplicateGroupInfo>>::failure(std::move(scanned).error());
    }

    std::vector<DuplicateGroupInfo> infos;
    infos.reserve(scanned.value().size());
    for (auto& group : scanned.value())
    {
        DuplicateGroupInfo info;
        info.files.reserve(group.paths.size());
        for (const auto& path : group.paths)
        {
            std::chrono::system_clock::time_point modifiedAt{};
            if (auto stat = m_fileSystemRepository.stat(path); stat.hasValue())
            {
                modifiedAt = stat.value().modificationDate();
            }
            info.files.push_back(DuplicateFileInfo{ path, modifiedAt });
        }
        info.group = std::move(group);
        infos.push_back(std::move(info));
    }

    return Result<std::vector<DuplicateGroupInfo>>::success(std::move(infos));
}
