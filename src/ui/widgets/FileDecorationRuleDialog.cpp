#include "FileDecorationRuleDialog.h"

#include <algorithm>
#include <vector>

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace
{
    // Spelled out in full so the user never has to go hunting for the syntax reference
    // (Architecture.md §14.29): '*'/'?' glob, '"'-quoting (with '""' for a literal '"') so a comma
    // can appear inside one pattern, and a leading '#' to mean "folders only".
    QString patternHelpText()
    {
        return QObject::tr("Comma-separated patterns; * = any run of characters, ? = one character.\n"
                            "Prefix a pattern with # to match folders instead of files.\n"
                            "Wrap a pattern in \"...\" to include a literal comma (use \"\" for a literal quote).\n\n"
                            "Examples:  *.exe   Image_?.png   #.git   \"Image, file.png\"");
    }

    // Chooses black or white text so the color name stays readable on any swatch background.
    QColor readableTextColorFor(const QColor& background)
    {
        const int brightness = (background.red() * 299 + background.green() * 587 + background.blue() * 114) / 1000;
        return brightness > 128 ? QColor(Qt::black) : QColor(Qt::white);
    }
}

FileDecorationRuleDialog::FileDecorationRuleDialog(const FileDecorationRule& initialRule, QWidget* parent, Kind kind)
    : QDialog(parent)
    , m_kind(kind)
    , m_rule(initialRule)
{
    switch (m_kind)
    {
        case Kind::PatternRule:
            setWindowTitle(tr("Edit Decoration Rule"));
            break;
        case Kind::HiddenFiles:
            setWindowTitle(tr("Edit Hidden Files Style"));
            break;
        case Kind::HiddenFolders:
            setWindowTitle(tr("Edit Hidden Folders Style"));
            break;
    }

    if (initialRule.hexColor())
    {
        m_color = QColor(QString::fromStdString(*initialRule.hexColor()));
    }

    auto* mainLayout = new QVBoxLayout(this);

    auto* helpLabel = new QLabel(patternHelpText(), this);
    helpLabel->setWordWrap(true);
    helpLabel->setVisible(m_kind == Kind::PatternRule);
    mainLayout->addWidget(helpLabel);

    auto* form = new QFormLayout();

    if (m_kind == Kind::PatternRule)
    {
        m_patternsEdit = new QLineEdit(QString::fromStdString(initialRule.patternsRaw()), this);
    }
    else
    {
        m_patternsEdit =
            new QLineEdit(m_kind == Kind::HiddenFiles ? tr("(all hidden files)") : tr("(all hidden folders)"), this);
        m_patternsEdit->setEnabled(false);
    }
    form->addRow(tr("Patterns:"), m_patternsEdit);

    auto* colorLayout = new QHBoxLayout();
    m_colorButton = new QPushButton(this);
    m_colorButton->setFixedWidth(100);
    m_colorButton->setToolTip(tr("Click to choose a color"));
    auto* resetColorButton = new QPushButton(tr("Reset"), this);
    colorLayout->addWidget(m_colorButton);
    colorLayout->addWidget(resetColorButton);
    colorLayout->addStretch();
    form->addRow(tr("Color:"), colorLayout);

    mainLayout->addLayout(form);

    auto* fontGroup = new QGroupBox(tr("Font"), this);
    auto* fontGrid = new QGridLayout(fontGroup);

    auto* familyLayout = new QVBoxLayout();
    m_fontFamilyEdit = new QLineEdit(fontGroup);
    m_fontFamilyEdit->setPlaceholderText(tr("Type to search fonts..."));
    familyLayout->addWidget(m_fontFamilyEdit);
    m_fontFamilyList = new QListWidget(fontGroup);
    const QStringList families = QFontDatabase::families();
    for (const QString& family : families)
    {
        new QListWidgetItem(family, m_fontFamilyList);
    }
    familyLayout->addWidget(m_fontFamilyList);
    fontGrid->addLayout(familyLayout, 0, 0, 2, 1);

    m_fontSizeList = new QListWidget(fontGroup);
    std::vector<int> sizes;
    for (int size : QFontDatabase::standardSizes())
    {
        sizes.push_back(size);
    }
    if (initialRule.fontPointSize() &&
        std::find(sizes.begin(), sizes.end(), *initialRule.fontPointSize()) == sizes.end())
    {
        sizes.push_back(*initialRule.fontPointSize());
    }
    std::sort(sizes.begin(), sizes.end());
    for (int size : sizes)
    {
        new QListWidgetItem(QString::number(size), m_fontSizeList);
    }
    fontGrid->addWidget(m_fontSizeList, 0, 1, 2, 1);

    auto* effectsLayout = new QVBoxLayout();
    m_boldCheckBox = new QCheckBox(tr("Bold"), fontGroup);
    m_italicCheckBox = new QCheckBox(tr("Italic"), fontGroup);
    m_underlineCheckBox = new QCheckBox(tr("Underline"), fontGroup);
    m_strikeoutCheckBox = new QCheckBox(tr("Strikeout"), fontGroup);
    m_boldCheckBox->setChecked(initialRule.bold());
    m_italicCheckBox->setChecked(initialRule.italic());
    m_underlineCheckBox->setChecked(initialRule.underline());
    m_strikeoutCheckBox->setChecked(initialRule.strikeout());
    effectsLayout->addWidget(m_boldCheckBox);
    effectsLayout->addWidget(m_italicCheckBox);
    effectsLayout->addWidget(m_underlineCheckBox);
    effectsLayout->addWidget(m_strikeoutCheckBox);
    effectsLayout->addStretch();
    fontGrid->addLayout(effectsLayout, 2, 0);

    m_previewLabel = new QLabel(tr("AaBbYyZz"), fontGroup);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setFrameShape(QFrame::Panel);
    m_previewLabel->setFrameShadow(QFrame::Sunken);
    m_previewLabel->setMinimumSize(140, 60);
    fontGrid->addWidget(m_previewLabel, 2, 1);

    fontGrid->setColumnStretch(0, 1);
    fontGrid->setRowStretch(1, 1);

    mainLayout->addWidget(fontGroup, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    connect(m_colorButton, &QPushButton::clicked, this, &FileDecorationRuleDialog::chooseColor);
    connect(resetColorButton, &QPushButton::clicked, this, &FileDecorationRuleDialog::resetColor);
    connect(m_fontFamilyEdit, &QLineEdit::textEdited, this, &FileDecorationRuleDialog::onFamilyEditTextChanged);
    connect(m_fontFamilyList, &QListWidget::currentRowChanged, this, &FileDecorationRuleDialog::onFamilyListRowChanged);
    connect(m_fontSizeList, &QListWidget::currentRowChanged, this, &FileDecorationRuleDialog::updatePreview);
    connect(m_boldCheckBox, &QCheckBox::toggled, this, &FileDecorationRuleDialog::updatePreview);
    connect(m_italicCheckBox, &QCheckBox::toggled, this, &FileDecorationRuleDialog::updatePreview);
    connect(m_underlineCheckBox, &QCheckBox::toggled, this, &FileDecorationRuleDialog::updatePreview);
    connect(m_strikeoutCheckBox, &QCheckBox::toggled, this, &FileDecorationRuleDialog::updatePreview);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &FileDecorationRuleDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &FileDecorationRuleDialog::reject);

    // Pre-fill the family edit and select the matching list row (if the family is installed),
    // and likewise pre-select the initial point size, without firing updatePreview() mid-setup --
    // it's called once explicitly below instead.
    if (initialRule.fontFamily())
    {
        const QString initialFamily = QString::fromStdString(*initialRule.fontFamily());
        m_fontFamilyEdit->setText(initialFamily);
        const int found = static_cast<int>(families.indexOf(initialFamily));
        if (found >= 0)
        {
            const QSignalBlocker blocker(m_fontFamilyList);
            m_fontFamilyList->setCurrentRow(found);
        }
    }
    if (initialRule.fontPointSize())
    {
        const QList<QListWidgetItem*> matches = m_fontSizeList->findItems(QString::number(*initialRule.fontPointSize()), Qt::MatchExactly);
        if (!matches.isEmpty())
        {
            m_fontSizeList->setCurrentItem(matches.first());
        }
    }

    updatePreview();
}

void FileDecorationRuleDialog::chooseColor()
{
    const QColor chosen = QColorDialog::getColor(m_color.value_or(QColor(Qt::black)), this, tr("Choose Color"));
    if (!chosen.isValid())
    {
        return;
    }
    m_color = chosen;
    updatePreview();
}

void FileDecorationRuleDialog::resetColor()
{
    m_color = std::nullopt;
    updatePreview();
}

void FileDecorationRuleDialog::onFamilyEditTextChanged(const QString& text)
{
    // Quick selection: jump the list to the first family that starts with what's been typed so
    // far, without touching the edit's own text (the user is still typing it).
    const QSignalBlocker blocker(m_fontFamilyList);
    if (text.isEmpty())
    {
        m_fontFamilyList->setCurrentRow(-1);
    }
    else
    {
        const QList<QListWidgetItem*> matches = m_fontFamilyList->findItems(text, Qt::MatchStartsWith);
        if (!matches.isEmpty())
        {
            m_fontFamilyList->setCurrentItem(matches.first());
            m_fontFamilyList->scrollToItem(matches.first());
        }
        else
        {
            m_fontFamilyList->setCurrentRow(-1);
        }
    }
    updatePreview();
}

void FileDecorationRuleDialog::onFamilyListRowChanged(int row)
{
    if (QListWidgetItem* item = m_fontFamilyList->item(row))
    {
        m_fontFamilyEdit->setText(item->text());
    }
    updatePreview();
}

void FileDecorationRuleDialog::updatePreview()
{
    QFont font = m_previewLabel->font();
    const QString family = m_fontFamilyEdit->text().trimmed();
    if (!family.isEmpty())
    {
        font.setFamily(family);
    }
    if (QListWidgetItem* currentSize = m_fontSizeList->currentItem())
    {
        font.setPointSize(currentSize->text().toInt());
    }
    font.setBold(m_boldCheckBox->isChecked());
    font.setItalic(m_italicCheckBox->isChecked());
    font.setUnderline(m_underlineCheckBox->isChecked());
    font.setStrikeOut(m_strikeoutCheckBox->isChecked());
    m_previewLabel->setFont(font);

    if (m_color)
    {
        m_previewLabel->setStyleSheet(QStringLiteral("color: %1;").arg(m_color->name()));
        m_colorButton->setText(m_color->name());
        m_colorButton->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                                          .arg(m_color->name(), readableTextColorFor(*m_color).name()));
    }
    else
    {
        m_previewLabel->setStyleSheet(QString());
        m_colorButton->setText(tr("(Default)"));
        m_colorButton->setStyleSheet(QString());
    }
}

void FileDecorationRuleDialog::accept()
{
    const std::string patternsRaw = m_kind == Kind::PatternRule ? m_patternsEdit->text().toStdString() : std::string();
    const std::optional<std::string> hexColor = m_color ? std::optional<std::string>(m_color->name().toStdString()) : std::nullopt;

    const QString familyText = m_fontFamilyEdit->text().trimmed();
    const std::optional<std::string> fontFamily = familyText.isEmpty() ? std::nullopt : std::optional<std::string>(familyText.toStdString());

    std::optional<int> fontPointSize;
    if (QListWidgetItem* currentSize = m_fontSizeList->currentItem())
    {
        fontPointSize = currentSize->text().toInt();
    }

    Result<FileDecorationRule> result =
        FileDecorationRule::create(patternsRaw, hexColor, fontFamily, fontPointSize, m_boldCheckBox->isChecked(),
                                    m_italicCheckBox->isChecked(), m_underlineCheckBox->isChecked(), m_strikeoutCheckBox->isChecked());
    if (!result)
    {
        m_patternsEdit->setStyleSheet(QStringLiteral("border: 1px solid red;"));
        m_patternsEdit->setToolTip(QString::fromStdString(result.error().message));
        return;
    }

    m_patternsEdit->setStyleSheet(QString());
    m_patternsEdit->setToolTip(QString());
    m_rule = std::move(result).value();

    QDialog::accept();
}
