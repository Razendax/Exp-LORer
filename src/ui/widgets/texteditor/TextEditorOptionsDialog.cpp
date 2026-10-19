#include "TextEditorOptionsDialog.h"

#include "LanguageRegistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

namespace
{
    // Same twelve languages/order as SettingsDialog's own per-language theme list.
    constexpr std::array<Language, 12> kAllLanguages = {
        Language::C,          Language::Cpp,        Language::CSharp,    Language::Python,     Language::JavaScript,
        Language::TypeScript, Language::Json,       Language::Html,      Language::Css,
        Language::Yaml,       Language::PowerShell, Language::Markdown,
    };
}

TextEditorOptionsDialog::TextEditorOptionsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Text Editor Options"));

    m_findCheckBox = new QCheckBox(tr("Find"), this);
    m_findCheckBox->setChecked(true);

    m_goToLineCheckBox = new QCheckBox(tr("Go to Line"), this);
    m_goToLineCheckBox->setChecked(true);

    m_lineNumbersCheckBox = new QCheckBox(tr("Line Numbers"), this);
    m_lineNumbersCheckBox->setChecked(true);

    m_multicursorCheckBox = new QCheckBox(tr("Multicursor"), this);
    m_multicursorCheckBox->setChecked(true);

    m_highlightSelectionOccurrencesCheckBox = new QCheckBox(tr("Highlight selected text occurrences"), this);
    m_highlightSelectionOccurrencesCheckBox->setChecked(true);

    m_readOnlyCheckBox = new QCheckBox(tr("Read-only"), this);
    m_readOnlyCheckBox->setChecked(false);

    m_highlightLanguageComboBox = new QComboBox(this);
    m_highlightLanguageComboBox->addItem(tr("Plain Text (no highlighting)"));
    for (const Language language : kAllLanguages)
        m_highlightLanguageComboBox->addItem(LanguageRegistry::displayName(language));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Open Editor"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_findCheckBox);
    layout->addWidget(m_goToLineCheckBox);
    layout->addWidget(m_lineNumbersCheckBox);
    layout->addWidget(m_multicursorCheckBox);
    layout->addWidget(m_highlightSelectionOccurrencesCheckBox);
    layout->addWidget(m_readOnlyCheckBox);
    layout->addWidget(m_highlightLanguageComboBox);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

TextEditorOptions TextEditorOptionsDialog::options() const
{
    TextEditorOptions result;
    result.findEnabled = m_findCheckBox->isChecked();
    result.goToLineEnabled = m_goToLineCheckBox->isChecked();
    result.lineNumbersEnabled = m_lineNumbersCheckBox->isChecked();
    result.multicursorEnabled = m_multicursorCheckBox->isChecked();
    result.highlightSelectionOccurrences = m_highlightSelectionOccurrencesCheckBox->isChecked();
    result.readOnly = m_readOnlyCheckBox->isChecked();

    const int languageIndex = m_highlightLanguageComboBox->currentIndex();
    result.highlightLanguage =
        languageIndex > 0 ? std::make_optional(kAllLanguages.at(static_cast<std::size_t>(languageIndex - 1)))
                           : std::nullopt;
    return result;
}
