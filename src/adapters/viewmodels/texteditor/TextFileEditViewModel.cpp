#include "TextFileEditViewModel.h"

#include <utility>

#include <QMetaObject>

#include "TextFileEditUseCase.h"
#include "TextFileEditWorker.h"

TextFileEditViewModel::TextFileEditViewModel(TextFileEditUseCase& textFileEditUseCase, QObject* parent)
    : QObject(parent)
    , m_textFileEditUseCase(textFileEditUseCase)
{
    m_worker = new TextFileEditWorker(m_textFileEditUseCase);
    m_worker->moveToThread(&m_workerThread);
    m_workerThread.start();
}

TextFileEditViewModel::~TextFileEditViewModel()
{
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
}

void TextFileEditViewModel::requestLoad(std::filesystem::path path)
{
    // Both hops here are queued functor calls, not Qt signals/slots -- same reasoning as
    // FilePreviewViewModel::dispatchGeneration: Result<std::string> isn't a Qt meta-type, so
    // routing it through Qt's meta-type system for cross-thread delivery would be an avoidable
    // risk. A queued functor just moves its captured state across threads as a plain closure.
    QMetaObject::invokeMethod(
        m_worker,
        [this, path = std::move(path)]() {
            Result<std::string> result = m_worker->load(path);

            QMetaObject::invokeMethod(
                this,
                [this, path, result = std::move(result)]() mutable {
                    if (!result)
                    {
                        emit loadFailed(path, QString::fromStdString(result.error().message));
                        return;
                    }
                    emit loadReady(path, QString::fromStdString(result.value()));
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void TextFileEditViewModel::requestSave(std::filesystem::path path, QString content)
{
    QMetaObject::invokeMethod(
        m_worker,
        [this, path = std::move(path), content]() {
            Result<void> result = m_worker->save(path, content.toStdString());

            QMetaObject::invokeMethod(
                this,
                [this, path, result = std::move(result)]() mutable {
                    if (!result)
                    {
                        emit saveFailed(path, QString::fromStdString(result.error().message));
                        return;
                    }
                    emit saveSucceeded(path);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
