#pragma once

#include <QFileIconProvider>
#include <QPixmap>
#include <QWidget>

class FilePreview;
class FilePreviewViewModel;
class QHideEvent;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QResizeEvent;
class QShowEvent;
class QStackedWidget;

// The Preview tab's content in the right panel (Architecture.md §14.25, Specification.md
// "Preview Panel"): a QStackedWidget switching between an image/poster label, a read-only
// monospace text view, a flat one-level folder listing (existing QFileIconProvider icons), and
// loading/unsupported/error states, driven by FilePreviewViewModel's signals. Calls
// setPanelActive(true/false) from showEvent/hideEvent so no background generation happens while
// this tab isn't the visible one.
class PreviewPanelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewPanelWidget(FilePreviewViewModel* viewModel, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void onPreviewLoading();
    void onPreviewReady(const FilePreview& preview);
    void onPreviewFailed(const QString& message);
    void onPreviewCleared();

    void showImage(const FilePreview& preview);
    void showText(const FilePreview& preview);
    void showFolder(const FilePreview& preview);
    void rescaleImageLabel();

    FilePreviewViewModel* m_viewModel = nullptr;

    QStackedWidget* m_stack = nullptr;
    QLabel* m_imageLabel = nullptr;
    QPlainTextEdit* m_textView = nullptr;
    QListWidget* m_folderList = nullptr;
    QLabel* m_loadingLabel = nullptr;
    QLabel* m_unsupportedLabel = nullptr;
    QLabel* m_errorLabel = nullptr;

    QFileIconProvider m_iconProvider;
    QPixmap m_currentImage;
};
