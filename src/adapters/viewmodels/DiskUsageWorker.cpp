#include "DiskUsageWorker.h"

#include "DiskUsageUseCase.h"
#include "SQLiteDiskUsageRepository.h"

DiskUsageWorker::DiskUsageWorker(IDiskUsageScanner& scanner, const std::filesystem::path& databasePath, QObject* parent)
    : QObject(parent)
    , m_scanner(scanner)
    , m_databasePath(databasePath)
{
}

DiskUsageWorker::~DiskUsageWorker() = default;

void DiskUsageWorker::ensureUseCase()
{
    if (m_useCase)
    {
        return;
    }

    m_repository = std::make_unique<SQLiteDiskUsageRepository>(m_databasePath);
    m_useCase = std::make_unique<DiskUsageUseCase>(m_scanner, *m_repository);
}

Result<void> DiskUsageWorker::scanAndPersist(const std::filesystem::path& root)
{
    ensureUseCase();

    auto result = m_useCase->scanAndPersist(root);
    if (result.hasError())
    {
        return Result<void>::failure(std::move(result).error());
    }

    return Result<void>::success();
}

Result<std::vector<DiskUsageNode>> DiskUsageWorker::childrenFor(const std::filesystem::path& folder)
{
    ensureUseCase();

    return m_useCase->childrenFor(folder);
}
