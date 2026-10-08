#include "TextPreviewWidget.h"

#include <QFont>

#include "FilePreview.h"
#include "LanguageRegistry.h"
#include "MediaExtensions.h"
#include "SyntaxHighlighter.h"

TextPreviewWidget::TextPreviewWidget(SyntaxHighlightEngine& syntaxHighlightEngine,
                                      HighlightThemeViewModel& highlightThemeViewModel, QWidget* parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("previewTextView"));
    setReadOnly(true);
    QFont monospaceFont("Consolas");
    monospaceFont.setStyleHint(QFont::Monospace);
    setFont(monospaceFont);

    m_syntaxHighlighter = new SyntaxHighlighter(document(), syntaxHighlightEngine, highlightThemeViewModel);
}

void TextPreviewWidget::setPreview(const FilePreview& preview)
{
    QString content = QString::fromUtf8(preview.text().data(), static_cast<int>(preview.text().size()));
    if (preview.textTruncated())
    {
        content += tr("\n\n[...truncated...]");
    }

    // QSyntaxHighlighter reformats automatically on every QTextDocument::contentsChange, so
    // setPlainText() below would otherwise trigger one highlightBlock pass using whatever language
    // the highlighter was still holding from the previous preview (a visible flash of wrong-language
    // highlighting, and wasted work). Block the document's signals for that one call so the only
    // highlight pass is the correct one setLanguage() triggers explicitly right after, against both
    // the new text and the new language.
    document()->blockSignals(true);
    setPlainText(content);
    document()->blockSignals(false);

    const std::string extension = MediaExtensions::lowercaseExtension(preview.path());
    m_syntaxHighlighter->setLanguage(LanguageRegistry::languageForExtension(extension));
}
