#include "TextFileEditWorker.h"

#include "TextFileEditUseCase.h"

TextFileEditWorker::TextFileEditWorker(TextFileEditUseCase& useCase, QObject* parent)
    : QObject(parent)
    , m_useCase(useCase)
{
}

Result<std::string> TextFileEditWorker::load(const std::filesystem::path& path) const
{
    return m_useCase.readFullText(path);
}

Result<void> TextFileEditWorker::save(const std::filesystem::path& path, const std::string& content) const
{
    return m_useCase.writeText(path, content);
}
