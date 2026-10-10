#pragma once

#include <filesystem>

#include <QWidget>

class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QSlider;
class QToolButton;
class QVideoWidget;

// Standalone, reusable inline video player (Architecture.md §14.67): wraps QMediaPlayer +
// QAudioOutput + QVideoWidget behind play/pause, stop, a seek bar, and a volume control. Lives in
// its own folder rather than under preview/ because it is not structurally tied to the Preview
// tab -- PreviewPanelWidget is just its first consumer.
class VideoPlayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlayerWidget(QWidget* parent = nullptr);

    // Loads `path` as the current source and leaves playback paused (first frame visible, no
    // autoplay/audio burst on selection).
    void load(const std::filesystem::path& path);

    // Stops playback and clears the source, so playback/audio never continues once this widget is
    // navigated away from (hidden, selection changed, or superseded by a newer preview).
    void release();

private:
    void togglePlayPause();
    void stop();
    void toggleMute();
    void onDurationChanged(qint64 durationMs);
    void onPositionChanged(qint64 positionMs);
    void onPlaybackStateChanged();
    void onSeekSliderPressed();
    void onSeekSliderMoved(int value);
    void onSeekSliderReleased();
    void updateTimeLabel();

    QMediaPlayer* m_player = nullptr;
    QAudioOutput* m_audioOutput = nullptr;
    QVideoWidget* m_videoWidget = nullptr;

    QToolButton* m_playPauseButton = nullptr; // objectName "videoPlayPauseButton"
    QToolButton* m_stopButton = nullptr;      // objectName "videoStopButton"
    QLabel* m_timeLabel = nullptr;            // objectName "videoTimeLabel"
    QSlider* m_seekSlider = nullptr;          // objectName "videoSeekSlider"
    QToolButton* m_muteButton = nullptr;      // objectName "videoMuteButton"
    QSlider* m_volumeSlider = nullptr;        // objectName "videoVolumeSlider"

    bool m_seekSliderBeingDragged = false;
    qreal m_volumeBeforeMute = 1.0;
};
