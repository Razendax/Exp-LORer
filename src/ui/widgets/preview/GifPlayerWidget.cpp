#include "GifPlayerWidget.h"

#include <QImageReader>
#include <QLabel>
#include <QMovie>
#include <QResizeEvent>
#include <QVBoxLayout>

GifPlayerWidget::GifPlayerWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_label = new QLabel(this);
    m_label->setObjectName(QStringLiteral("gifPlayerLabel"));
    m_label->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_label);
}

void GifPlayerWidget::load(const std::filesystem::path& path)
{
    const QString fileName = QString::fromStdWString(path.wstring());

    // Cheap header-only read of the GIF's native frame size, same "metadata without a full
    // decode" rationale VipsImageDecoder uses for its own header-level open.
    m_nativeSize = QImageReader(fileName).size();

    delete m_movie;
    m_movie = new QMovie(fileName, QByteArray(), this);
    m_label->setMovie(m_movie);
    rescaleMovie();
    m_movie->start();
}

void GifPlayerWidget::release()
{
    if (m_movie)
    {
        m_movie->stop();
    }
    m_label->setMovie(nullptr);
    delete m_movie;
    m_movie = nullptr;
    m_nativeSize = QSize();
}

void GifPlayerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rescaleMovie();
}

void GifPlayerWidget::rescaleMovie()
{
    if (!m_movie || !m_nativeSize.isValid())
    {
        return;
    }

    const QSize scaled = m_nativeSize.scaled(m_label->size(), Qt::KeepAspectRatio);
    m_movie->setScaledSize(scaled);
}
