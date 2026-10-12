#pragma once

#include <filesystem>

#include <QSize>
#include <QWidget>

class QLabel;
class QMovie;
class QResizeEvent;

// Minimal animated-GIF player for the Preview tab's gif dropdown (Architecture.md §14.79): a
// QLabel driven by QMovie, with the same load(path)/release() two-method contract
// VideoPlayerWidget uses so PreviewPanelWidget can integrate it identically. No play/pause/seek
// controls -- GIFs just loop automatically per their own embedded loop-count metadata (QMovie's
// default behavior). Lives under preview/ rather than its own top-level folder like
// video/VideoPlayerWidget, since nothing else in the codebase needs to reuse it.
class GifPlayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GifPlayerWidget(QWidget* parent = nullptr);

    // Loads and starts playing `path`. Stores its native frame size (a cheap header-only read) so
    // resizeEvent() can keep the scaled playback size in sync with this widget's size.
    void load(const std::filesystem::path& path);

    // Stops playback and clears the source, so decoding never continues once this widget is
    // navigated away from.
    void release();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void rescaleMovie();

    QLabel* m_label = nullptr;
    QMovie* m_movie = nullptr;
    QSize m_nativeSize;
};
