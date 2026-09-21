#pragma once

#include <optional>

#include <QColor>
#include <QDialog>
#include <QFont>
#include <QString>

#include "FileDecorationRule.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// One rule's editor, opened from SettingsDialog's General -> Colors page (Architecture.md
// §14.29) both for "Add Rule" and for editing an existing row. OK validates the pattern text via
// FileDecorationRule::create() and refuses to close on a parse failure (an unterminated `"`),
// showing the error inline rather than silently discarding the edit.
class FileDecorationRuleDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FileDecorationRuleDialog(const FileDecorationRule& initialRule, QWidget* parent = nullptr);

    // Only meaningful after exec() returns QDialog::Accepted.
    const FileDecorationRule& rule() const { return m_rule; }

private:
    void chooseColor();
    void resetColor();
    void onFamilyEditTextChanged(const QString& text);
    void onFamilyListRowChanged(int row);
    void updatePreview();
    void accept() override;

    QLineEdit* m_patternsEdit = nullptr;
    QPushButton* m_colorButton = nullptr;
    QLineEdit* m_fontFamilyEdit = nullptr;
    QListWidget* m_fontFamilyList = nullptr;
    QListWidget* m_fontSizeList = nullptr;
    QCheckBox* m_boldCheckBox = nullptr;
    QCheckBox* m_italicCheckBox = nullptr;
    QCheckBox* m_underlineCheckBox = nullptr;
    QCheckBox* m_strikeoutCheckBox = nullptr;
    QLabel* m_previewLabel = nullptr;

    std::optional<QColor> m_color;

    FileDecorationRule m_rule;
};
