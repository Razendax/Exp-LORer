#include "FragmentationWorker.h"

#include "FragmentationUseCase.h"
#include "SQLiteFragmentationRepository.h"

FragmentationWorker::FragmentationWorker(IFragmentationScanner& scanner, const std::filesystem::path& databasePath, QObject* parent)
    : QObject(parent)
    , m_scanner(scanner)
    , m_databasePath(databasePath)
{
}

FragmentationWorker::~FragmentationWorker() = default;

void FragmentationWorker::ensureUseCase()
{
    if (m_useCase)
    {
        return;
    }

    m_repository = std::make_unique<SQLiteFragmentationRepository>(m_databasePath);
    m_useCase = std::make_unique<FragmentationUseCase>(m_scanner, *m_repository);
}

Result<void> FragmentationWorker::scanAndPersist(
    const std::filesystem::path& root, std::uintmax_t minFileSizeBytes,
    const std::shared_ptr<std::atomic<bool>>& cancelFlag,
    const std::function<void(std::size_t, std::size_t, const std::filesystem::path&)>& onProgress)
{
    ensureUseCase();

    auto result = m_useCase->scanAndPersist(root, minFileSizeBytes, onProgress, *cancelFlag);
    if (result.hasError())
    {
        return Result<void>::failure(std::move(result).error());
    }

    return Result<void>::success();
}

Result<std::vector<FileFragmentationInfo>> FragmentationWorker::filesUnder(const std::filesystem::path& folder)
{
    ensureUseCase();

    return m_useCase->filesUnder(folder);
}
