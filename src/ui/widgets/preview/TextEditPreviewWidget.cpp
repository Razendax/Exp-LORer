#include "TextEditPreviewWidget.h"

#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include "FilePreview.h"
#include "LanguageRegistry.h"
#include "TextEditorPanel.h"
#include "TextEditorWidget.h"
#include "TextFileEditViewModel.h"

TextEditPreviewWidget::TextEditPreviewWidget(TextFileEditViewModel& textFileEditViewModel,
                                               SyntaxHighlightEngine& syntaxHighlightEngine,
                                               HighlightThemeViewModel& highlightThemeViewModel, QWidget* parent)
    : QWidget(parent)
    , m_textFileEditViewModel(textFileEditViewModel)
    , m_syntaxHighlightEngine(syntaxHighlightEngine)
    , m_highlightThemeViewModel(highlightThemeViewModel)
{
    setObjectName(QStringLiteral("textEditPreviewWidget"));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);

    auto* toolbarLayout = new QHBoxLayout();
    m_editButton = new QToolButton(this);
    m_editButton->setObjectName(QStringLiteral("previewEditButton"));
    m_editButton->setText(tr("Edit"));
    toolbarLayout->addWidget(m_editButton);

    m_revertButton = new QToolButton(this);
    m_revertButton->setObjectName(QStringLiteral("previewRevertButton"));
    m_revertButton->setText(tr("Revert"));
    m_revertButton->setEnabled(false);
    toolbarLayout->addWidget(m_revertButton);

    m_saveButton = new QToolButton(this);
    m_saveButton->setObjectName(QStringLiteral("previewSaveButton"));
    m_saveButton->setText(tr("Save"));
    m_saveButton->setEnabled(false);
    toolbarLayout->addWidget(m_saveButton);

    toolbarLayout->addStretch(1);
    m_layout->addLayout(toolbarLayout);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("previewTextEditErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_errorLabel->hide();
    m_layout->addWidget(m_errorLabel);

    connect(m_editButton, &QToolButton::clicked, this, &TextEditPreviewWidget::onEditClicked);
    connect(m_revertButton, &QToolButton::clicked, this, &TextEditPreviewWidget::onRevertClicked);
    connect(m_saveButton, &QToolButton::clicked, this, &TextEditPreviewWidget::onSaveClicked);

    connect(&m_textFileEditViewModel, &TextFileEditViewModel::loadReady, this, &TextEditPreviewWidget::onLoadReady);
    connect(&m_textFileEditViewModel, &TextFileEditViewModel::loadFailed, this, &TextEditPreviewWidget::onLoadFailed);
    connect(&m_textFileEditViewModel, &TextFileEditViewModel::saveSucceeded, this, &TextEditPreviewWidget::onSaveSucceeded);
    connect(&m_textFileEditViewModel, &TextFileEditViewModel::saveFailed, this, &TextEditPreviewWidget::onSaveFailed);

    // Qt::WidgetWithChildrenShortcut -- same scoping convention TextEditorFindBar/
    // TextEditorGotoLineBar already use for Ctrl+F/Ctrl+G, so embedding this widget doesn't steal
    // Ctrl+S from unrelated widgets elsewhere in the window.
    auto* saveShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, &TextEditPreviewWidget::onSaveClicked);
}

void TextEditPreviewWidget::setPreview(const FilePreview& preview)
{
    // The Preview panel's directoryContentsChanged funnel re-fires for the SAME file after this
    // very widget's own Save writes it to disk and the live directory watcher picks that change
    // back up (Architecture.md §14.98) -- at that point the freshly-generated preview is, by
    // construction, identical to what's already on screen. Rebuilding the panel from scratch here
    // would silently kick the user out of edit mode (back to the read-only default) right after
    // they hit Save, for no visible benefit, so skip it while they're still actively editing this
    // same path.
    if (m_editorPanel && preview.path() == m_currentPath && !m_editorPanel->editor()->isReadOnly())
    {
        return;
    }

    m_currentPath = preview.path();
    m_lastLoadedContent.clear();
    m_requestInFlight = false;
    clearError();

    TextEditorOptions options = defaultOptions();
    options.readOnly = true;
    options.highlightLanguage = LanguageRegistry::languageForPath(preview.path());

    // Reuse the existing panel for the common plain-browsing case instead of tearing it down and
    // rebuilding it (editor + find/go-to-line bars + syntax highlighter) for every single
    // selection change -- only do that when something it was actually built with has changed.
    // isReadOnly() being false here means the previous panel is still an open edit session (the
    // caller already resolved any unsaved changes via prepareForTargetChange()) and must still be
    // rebuilt fresh, same as before.
    const bool canReuseExistingPanel =
        m_editorPanel && m_editorPanel->editor()->isReadOnly() && options.findEnabled == m_currentPanelOptions.findEnabled
        && options.goToLineEnabled == m_currentPanelOptions.goToLineEnabled
        && options.lineNumbersEnabled == m_currentPanelOptions.lineNumbersEnabled
        && options.multicursorEnabled == m_currentPanelOptions.multicursorEnabled
        && options.highlightSelectionOccurrences == m_currentPanelOptions.highlightSelectionOccurrences
        && options.highlightLanguage == m_currentPanelOptions.highlightLanguage;

    if (!canReuseExistingPanel)
    {
        delete m_editorPanel;

        m_editorPanel = new TextEditorPanel(options, this, &m_syntaxHighlightEngine, &m_highlightThemeViewModel);
        m_editorPanel->editor()->setObjectName(QStringLiteral("previewTextView"));
        QFont monospaceFont(QStringLiteral("Consolas"));
        monospaceFont.setStyleHint(QFont::Monospace);
        m_editorPanel->editor()->setFont(monospaceFont);
        m_layout->addWidget(m_editorPanel, 1);
        m_currentPanelOptions = options;
    }

    QString content = QString::fromUtf8(preview.text().data(), static_cast<int>(preview.text().size()));
    if (preview.textTruncated())
    {
        content += tr("\n\n[...truncated...]");
    }
    m_editorPanel->editor()->setPlainText(content);
    reapplyContentSearchHighlight();

    m_editButton->setEnabled(true);
    m_revertButton->setEnabled(false);
    m_saveButton->setEnabled(false);
}

void TextEditPreviewWidget::setContentSearchHighlight(const QString& query, const TextMatchOptions& options)
{
    m_contentSearchQuery = query;
    m_contentSearchOptions = options;
    reapplyContentSearchHighlight();
}

void TextEditPreviewWidget::reapplyContentSearchHighlight()
{
    if (!m_editorPanel)
    {
        return;
    }

    m_editorPanel->editor()->setContentSearchHighlight(m_contentSearchQuery, m_contentSearchOptions);
}

void TextEditPreviewWidget::goToLine(int oneBasedLine)
{
    if (!m_editorPanel)
    {
        return;
    }

    m_editorPanel->editor()->goToLine(oneBasedLine);
}

void TextEditPreviewWidget::prepareForTargetChange()
{
    if (!m_editorPanel || !m_editorPanel->editor()->document()->isModified())
    {
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(tr("\"%1\" has unsaved changes.").arg(QString::fromStdWString(m_currentPath.filename().wstring())));
    QPushButton* saveButton = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    box.addButton(tr("Don't Save"), QMessageBox::DestructiveRole);
    box.setDefaultButton(saveButton);
    box.exec();

    if (box.clickedButton() == saveButton)
    {
        // Best-effort: this widget's editor is about to be torn down by the next setPreview()
        // call either way, so a failure here has no UI left to report into (Architecture.md
        // §14.98's documented limitation).
        m_textFileEditViewModel.requestSave(m_currentPath, m_editorPanel->editor()->toPlainText());
    }
}

void TextEditPreviewWidget::onEditClicked()
{
    if (m_requestInFlight || !m_editorPanel)
    {
        return;
    }

    m_requestInFlight = true;
    m_editButton->setEnabled(false);
    clearError();
    m_textFileEditViewModel.requestLoad(m_currentPath);
}

void TextEditPreviewWidget::onRevertClicked()
{
    if (!m_editorPanel)
    {
        return;
    }

    // setPlainText() also clears the document's modified flag, which fires
    // modificationChanged(false) -- onDocumentModificationChanged disables m_saveButton for free.
    m_editorPanel->editor()->setPlainText(m_lastLoadedContent);
    reapplyContentSearchHighlight();
}

void TextEditPreviewWidget::onSaveClicked()
{
    if (m_requestInFlight || !m_editorPanel || !m_saveButton->isEnabled())
    {
        return;
    }

    m_requestInFlight = true;
    m_saveButton->setEnabled(false);
    clearError();
    m_textFileEditViewModel.requestSave(m_currentPath, m_editorPanel->editor()->toPlainText());
}

void TextEditPreviewWidget::onLoadReady(const std::filesystem::path& path, const QString& content)
{
    // Stale response for a path the widget has since navigated away from (setPreview() moved
    // m_currentPath on while this load was still in flight) -- the panel it was meant for no
    // longer exists, so applying it now would write this file's content into whatever is on
    // screen instead.
    if (path != m_currentPath || !m_editorPanel)
    {
        return;
    }

    m_requestInFlight = false;
    m_lastLoadedContent = content;
    m_editorPanel->editor()->setPlainText(content);
    reapplyContentSearchHighlight();
    m_editorPanel->setReadOnly(false);
    connect(m_editorPanel->editor()->document(), &QTextDocument::modificationChanged, this,
            &TextEditPreviewWidget::onDocumentModificationChanged, Qt::UniqueConnection);
    m_revertButton->setEnabled(true);
    m_saveButton->setEnabled(false);
}

void TextEditPreviewWidget::onLoadFailed(const std::filesystem::path& path, const QString& message)
{
    // See onLoadReady(): ignore a response for a path the widget has moved on from.
    if (path != m_currentPath)
    {
        return;
    }

    m_requestInFlight = false;
    m_editButton->setEnabled(true);
    showError(message);
}

void TextEditPreviewWidget::onSaveSucceeded(const std::filesystem::path& path)
{
    // See onLoadReady(): this save may be the best-effort one fired from prepareForTargetChange()
    // for a file setPreview() has since swapped away from -- its panel is gone, there is nothing
    // left here to mark unmodified.
    if (path != m_currentPath || !m_editorPanel)
    {
        return;
    }

    m_requestInFlight = false;

    // Changing from true -> false here fires modificationChanged(false), which
    // onDocumentModificationChanged already handles by disabling m_saveButton -- no duplicate
    // logic needed. Stays in edit mode, per spec.
    m_editorPanel->editor()->document()->setModified(false);
}

void TextEditPreviewWidget::onSaveFailed(const std::filesystem::path& path, const QString& message)
{
    // See onLoadReady(): ignore a response for a path the widget has moved on from.
    if (path != m_currentPath)
    {
        return;
    }

    m_requestInFlight = false;
    showError(message);

    if (m_editorPanel && m_editorPanel->editor()->document()->isModified())
    {
        m_saveButton->setEnabled(true);
    }
}

void TextEditPreviewWidget::onDocumentModificationChanged(bool modified)
{
    m_saveButton->setEnabled(modified);
}

void TextEditPreviewWidget::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

void TextEditPreviewWidget::clearError()
{
    m_errorLabel->clear();
    m_errorLabel->hide();
}

TextEditorOptions TextEditPreviewWidget::defaultOptions()
{
    TextEditorOptions options;
    options.lineNumbersEnabled = lineNumbersEnabled();
    options.findEnabled = findEnabled();
    options.goToLineEnabled = goToLineEnabled();
    options.multicursorEnabled = multicursorEnabled();
    options.highlightSelectionOccurrences = highlightSelectionOccurrencesEnabled();
    return options;
}

namespace
{
    bool settingsBool(const char* key)
    {
        return QSettings().value(QLatin1String(key), true).toBool();
    }
}

bool TextEditPreviewWidget::lineNumbersEnabled()
{
    return settingsBool(kLineNumbersEnabledSettingsKey);
}

bool TextEditPreviewWidget::findEnabled()
{
    return settingsBool(kFindEnabledSettingsKey);
}

bool TextEditPreviewWidget::goToLineEnabled()
{
    return settingsBool(kGoToLineEnabledSettingsKey);
}

bool TextEditPreviewWidget::multicursorEnabled()
{
    return settingsBool(kMulticursorEnabledSettingsKey);
}

bool TextEditPreviewWidget::highlightSelectionOccurrencesEnabled()
{
    return settingsBool(kHighlightSelectionOccurrencesEnabledSettingsKey);
}
