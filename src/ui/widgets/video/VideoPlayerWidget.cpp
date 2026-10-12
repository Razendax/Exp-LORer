#include "VideoPlayerWidget.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace
{
QString formatTime(qint64 milliseconds)
{
    if (milliseconds < 0)
        milliseconds = 0;
    const qint64 totalSeconds = milliseconds / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QChar(u'0'));
}

// Plain QSlider only pages by a single step when the groove (rather than the handle) is clicked.
// A scrub bar should jump straight to the clicked position, like every other media player's seek
// bar, so this subclass computes the clicked value and warps the handle there before letting the
// base class process the press -- at that point the handle is already under the cursor, so the
// base class starts an ordinary drag (still emitting sliderPressed/sliderMoved/sliderReleased as
// usual) instead of taking a single page step.
class ClickToSeekSlider : public QSlider
{
public:
    explicit ClickToSeekSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            QStyleOptionSlider opt;
            initStyleOption(&opt);
            const QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            const QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

            const int grooveSpan = grooveRect.width() - handleRect.width();
            const int clickPos = event->position().toPoint().x() - grooveRect.x() - handleRect.width() / 2;

            setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), clickPos, grooveSpan));
            event->accept();
        }
        QSlider::mousePressEvent(event);
    }
};
} // namespace

VideoPlayerWidget::VideoPlayerWidget(QWidget* parent)
    : QWidget(parent)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

    // QAudioOutput only resolves the default output device once, at construction -- it does not
    // follow later OS default-device changes (switching the system's default playback device, or
    // Windows auto-switching on a headphone plug/unplug) on its own, which otherwise leaves
    // playback stuck on whatever device was default when the widget was created.
    m_mediaDevices = new QMediaDevices(this);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &VideoPlayerWidget::followDefaultAudioOutputDevice);

    m_videoWidget = new QVideoWidget(this);
    m_player->setVideoOutput(m_videoWidget);

    m_playPauseButton = new QToolButton(this);
    m_playPauseButton->setObjectName(QStringLiteral("videoPlayPauseButton"));
    m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    m_stopButton = new QToolButton(this);
    m_stopButton->setObjectName(QStringLiteral("videoStopButton"));
    m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    m_timeLabel = new QLabel(this);
    m_timeLabel->setObjectName(QStringLiteral("videoTimeLabel"));
    updateTimeLabel();

    m_seekSlider = new ClickToSeekSlider(Qt::Horizontal, this);
    m_seekSlider->setObjectName(QStringLiteral("videoSeekSlider"));
    m_seekSlider->setRange(0, 0);

    m_muteButton = new QToolButton(this);
    m_muteButton->setObjectName(QStringLiteral("videoMuteButton"));
    m_muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setObjectName(QStringLiteral("videoVolumeSlider"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(80);
    m_audioOutput->setVolume(1.0);

    auto* controlsRow = new QHBoxLayout();
    controlsRow->addWidget(m_playPauseButton);
    controlsRow->addWidget(m_stopButton);
    controlsRow->addWidget(m_timeLabel);
    controlsRow->addStretch(1);

    auto* seekRow = new QHBoxLayout();
    seekRow->addWidget(m_seekSlider, 1);
    seekRow->addWidget(m_muteButton);
    seekRow->addWidget(m_volumeSlider);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoWidget, 1);
    layout->addLayout(controlsRow);
    layout->addLayout(seekRow);

    connect(m_playPauseButton, &QToolButton::clicked, this, &VideoPlayerWidget::togglePlayPause);
    connect(m_stopButton, &QToolButton::clicked, this, &VideoPlayerWidget::stop);
    connect(m_muteButton, &QToolButton::clicked, this, &VideoPlayerWidget::toggleMute);
    connect(m_volumeSlider, &QSlider::valueChanged, this,
            [this](int value) { m_audioOutput->setVolume(value / 100.0); });
    connect(m_seekSlider, &QSlider::sliderPressed, this, &VideoPlayerWidget::onSeekSliderPressed);
    connect(m_seekSlider, &QSlider::sliderMoved, this, &VideoPlayerWidget::onSeekSliderMoved);
    connect(m_seekSlider, &QSlider::sliderReleased, this, &VideoPlayerWidget::onSeekSliderReleased);
    connect(m_player, &QMediaPlayer::durationChanged, this, &VideoPlayerWidget::onDurationChanged);
    connect(m_player, &QMediaPlayer::positionChanged, this, &VideoPlayerWidget::onPositionChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &VideoPlayerWidget::onPlaybackStateChanged);
}

void VideoPlayerWidget::load(const std::filesystem::path& path)
{
    m_player->setSource(QUrl::fromLocalFile(QString::fromStdWString(path.wstring())));
    m_player->pause();
}

void VideoPlayerWidget::release()
{
    m_player->stop();
    m_player->setSource(QUrl());
}

void VideoPlayerWidget::togglePlayPause()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void VideoPlayerWidget::stop()
{
    m_player->stop();
}

void VideoPlayerWidget::toggleMute()
{
    if (m_audioOutput->isMuted())
    {
        m_audioOutput->setMuted(false);
        m_muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
        m_volumeSlider->setEnabled(true);
    }
    else
    {
        m_audioOutput->setMuted(true);
        m_muteButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolumeMuted));
        m_volumeSlider->setEnabled(false);
    }
}

void VideoPlayerWidget::onDurationChanged(qint64 durationMs)
{
    m_seekSlider->setRange(0, static_cast<int>(durationMs));
    updateTimeLabel();
}

void VideoPlayerWidget::onPositionChanged(qint64 positionMs)
{
    if (m_seekSliderBeingDragged)
        return;

    m_seekSlider->setValue(static_cast<int>(positionMs));
    updateTimeLabel();
}

void VideoPlayerWidget::onPlaybackStateChanged()
{
    const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
    m_playPauseButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));

    if (m_player->playbackState() == QMediaPlayer::StoppedState)
    {
        m_seekSlider->setValue(0);
        updateTimeLabel();
    }
}

void VideoPlayerWidget::onSeekSliderPressed()
{
    m_seekSliderBeingDragged = true;
}

void VideoPlayerWidget::onSeekSliderMoved(int value)
{
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(value), formatTime(m_player->duration())));
}

void VideoPlayerWidget::onSeekSliderReleased()
{
    m_seekSliderBeingDragged = false;
    m_player->setPosition(m_seekSlider->value());
}

void VideoPlayerWidget::updateTimeLabel()
{
    m_timeLabel->setText(QStringLiteral("%1 / %2").arg(formatTime(m_player->position()), formatTime(m_player->duration())));
}

void VideoPlayerWidget::followDefaultAudioOutputDevice()
{
    const QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (defaultDevice != m_audioOutput->device())
        m_audioOutput->setDevice(defaultDevice);
}
