#include "SettingsDialog.h"

#include <array>
#include <memory>
#include <utility>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ColumnVisibilitySettingsViewModel.h"
#include "FileDecorationRuleDialog.h"
#include "FileDecorationsViewModel.h"
#include "FileListModel.h"
#include "HighlightThemeViewModel.h"
#include "NotificationSettingsViewModel.h"
#include "SyntaxHighlightEngine.h"
#include "WorkspacePaneWidget.h"

namespace
{
    constexpr std::array<Language, 9> kAllLanguages = {
        Language::C,      Language::Cpp,        Language::CSharp, Language::Python, Language::JavaScript,
        Language::TypeScript, Language::Json,   Language::Html,   Language::Css,
    };

    QString languageDisplayName(Language language)
    {
        switch (language)
        {
            case Language::C:
                return QObject::tr("C");
            case Language::Cpp:
                return QObject::tr("C++");
            case Language::CSharp:
                return QObject::tr("C#");
            case Language::Python:
                return QObject::tr("Python");
            case Language::JavaScript:
                return QObject::tr("JavaScript");
            case Language::TypeScript:
                return QObject::tr("TypeScript");
            case Language::Json:
                return QObject::tr("JSON");
            case Language::Html:
                return QObject::tr("HTML");
            case Language::Css:
                return QObject::tr("CSS");
        }
        return QString();
    }

    constexpr int kLanguageIndexRole = Qt::UserRole;

    // Every column except NameColumn, in the same order the Columns page's table and the header's
    // per-tab right-click menu both present them (Architecture.md §14.x).
    constexpr std::array<FileListModel::Column, FileListModel::ColumnCount - 1> kAllVisibilityColumns = {
        FileListModel::SizeColumn,      FileListModel::TypeColumn,   FileListModel::DateModifiedColumn,
        FileListModel::GitStatusColumn, FileListModel::TagsColumn,   FileListModel::DateCreatedColumn,
        FileListModel::PercentColumn,
    };

    // Mirrors FileListModel::headerData()'s own labels for these columns.
    QString columnDisplayName(FileListModel::Column column)
    {
        switch (column)
        {
            case FileListModel::SizeColumn:
                return QObject::tr("Size");
            case FileListModel::TypeColumn:
                return QObject::tr("Type");
            case FileListModel::DateModifiedColumn:
                return QObject::tr("Date modified");
            case FileListModel::GitStatusColumn:
                return QObject::tr("Git");
            case FileListModel::TagsColumn:
                return QObject::tr("Tags");
            case FileListModel::DateCreatedColumn:
                return QObject::tr("Date created");
            case FileListModel::PercentColumn:
                return QObject::tr("Percent");
            default:
                return QString();
        }
    }
}

SettingsDialog::SettingsDialog(SyntaxHighlightEngine& engine, HighlightThemeViewModel& themeViewModel,
                               FileDecorationsViewModel& fileDecorationsViewModel,
                               NotificationSettingsViewModel& notificationSettingsViewModel,
                               ColumnVisibilitySettingsViewModel& columnVisibilitySettingsViewModel, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_themeViewModel(themeViewModel)
    , m_fileDecorationsViewModel(fileDecorationsViewModel)
    , m_notificationSettingsViewModel(notificationSettingsViewModel)
    , m_columnVisibilitySettingsViewModel(columnVisibilitySettingsViewModel)
    , m_decorationRules(fileDecorationsViewModel.rules())
    , m_hiddenFilesRule(fileDecorationsViewModel.hiddenFilesRule())
    , m_hiddenFoldersRule(fileDecorationsViewModel.hiddenFoldersRule())
{
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));
    resize(700, 500);

    auto* mainLayout = new QVBoxLayout(this);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_categoryTree = new QTreeWidget(splitter);
    m_categoryTree->setObjectName(QStringLiteral("settingsCategoryTree"));
    m_categoryTree->setHeaderHidden(true);
    m_categoryTree->setMaximumWidth(220);

    auto* highlightRoot = new QTreeWidgetItem(m_categoryTree, QStringList{ tr("Highlight") });
    highlightRoot->setData(0, kLanguageIndexRole, -1);
    auto* languagesRoot = new QTreeWidgetItem(highlightRoot, QStringList{ tr("Languages") });
    languagesRoot->setData(0, kLanguageIndexRole, -1);

    m_pageStack = new QStackedWidget(splitter);
    m_pageStack->setObjectName(QStringLiteral("settingsPageStack"));
    m_languagePages.assign(kAllLanguages.size(), nullptr);

    for (std::size_t i = 0; i < kAllLanguages.size(); ++i)
    {
        const Language language = kAllLanguages[i];

        auto* item = new QTreeWidgetItem(languagesRoot, QStringList{ languageDisplayName(language) });
        item->setData(0, kLanguageIndexRole, static_cast<int>(i));

        // Cheap placeholder -- ensureLanguagePageBuilt() swaps in the real page (built via
        // buildLanguagePage/captureNamesFor, which compiles this language's tree-sitter query) only
        // once this language is actually selected.
        m_pageStack->addWidget(new QWidget(m_pageStack));
    }

    auto* generalRoot = new QTreeWidgetItem(m_categoryTree, QStringList{ tr("General") });
    generalRoot->setData(0, kLanguageIndexRole, -1);
    auto* uiItem = new QTreeWidgetItem(generalRoot, QStringList{ tr("UI") });
    uiItem->setData(0, kLanguageIndexRole, static_cast<int>(kAllLanguages.size()));
    auto* colorsItem = new QTreeWidgetItem(generalRoot, QStringList{ tr("Colors") });
    colorsItem->setData(0, kLanguageIndexRole, static_cast<int>(kAllLanguages.size()) + 1);
    auto* notificationsItem = new QTreeWidgetItem(generalRoot, QStringList{ tr("Notifications") });
    notificationsItem->setData(0, kLanguageIndexRole, static_cast<int>(kAllLanguages.size()) + 2);
    auto* columnsItem = new QTreeWidgetItem(generalRoot, QStringList{ tr("Columns") });
    columnsItem->setData(0, kLanguageIndexRole, static_cast<int>(kAllLanguages.size()) + 3);

    // All four built eagerly (unlike the lazily-built language pages above) since they're cheap --
    // no tree-sitter/query compilation involved.
    m_pageStack->addWidget(buildGeneralUiPage());
    m_pageStack->addWidget(buildFileDecorationsPage());
    m_pageStack->addWidget(buildNotificationsPage());
    m_pageStack->addWidget(buildColumnsPage());

    m_categoryTree->expandAll();

    splitter->addWidget(m_categoryTree);
    splitter->addWidget(m_pageStack);
    splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(splitter, 1);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("settingsCloseButton"));
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_categoryTree, &QTreeWidget::currentItemChanged, this, &SettingsDialog::onCategoryChanged);
    connect(&m_themeViewModel, &HighlightThemeViewModel::themeChanged, this, &SettingsDialog::refreshSwatchColors);

    if (!kAllLanguages.empty())
    {
        m_categoryTree->setCurrentItem(languagesRoot->child(0));
    }
}

void SettingsDialog::onCategoryChanged(QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/)
{
    if (!current)
    {
        return;
    }

    const int index = current->data(0, kLanguageIndexRole).toInt();
    if (index < 0)
    {
        return;
    }

    // Indices >= kAllLanguages.size() are the General pages (UI, Colors, ...), all built eagerly
    // in the constructor and added to the stack in tree order, so they need no lazy-build step.
    if (index < static_cast<int>(kAllLanguages.size()))
    {
        ensureLanguagePageBuilt(static_cast<std::size_t>(index));
    }

    m_pageStack->setCurrentIndex(index);
}

void SettingsDialog::ensureLanguagePageBuilt(std::size_t languageIndex)
{
    if (m_languagePages[languageIndex])
    {
        return;
    }

    QWidget* placeholder = m_pageStack->widget(static_cast<int>(languageIndex));
    QWidget* page = buildLanguagePage(kAllLanguages[languageIndex]);

    // Remove the placeholder before inserting the real page at the same index -- insertWidget()
    // shifts everything at/after that index forward by one, so inserting first (while the
    // placeholder still occupies the slot) would permanently misalign every later language's index
    // against its QTreeWidgetItem's kLanguageIndexRole.
    m_pageStack->removeWidget(placeholder);
    m_pageStack->insertWidget(static_cast<int>(languageIndex), page);
    placeholder->deleteLater();

    m_languagePages[languageIndex] = page;
}

QWidget* SettingsDialog::buildGeneralUiPage()
{
    auto* page = new QWidget(m_pageStack);
    auto* layout = new QVBoxLayout(page);

    m_showTabCloseButtonsCheckBox = new QCheckBox(tr("Show close button on tabs"), page);
    m_showTabCloseButtonsCheckBox->setObjectName(QStringLiteral("settingsShowTabCloseButtonsCheckBox"));
    m_showTabCloseButtonsCheckBox->setChecked(WorkspacePaneWidget::tabCloseButtonsEnabled());
    layout->addWidget(m_showTabCloseButtonsCheckBox);
    layout->addStretch(1);

    connect(m_showTabCloseButtonsCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue(QLatin1String(WorkspacePaneWidget::kShowTabCloseButtonsSettingsKey), checked);
        emit showTabCloseButtonsChanged(checked);
    });

    return page;
}

QWidget* SettingsDialog::buildLanguagePage(Language language)
{
    auto* page = new QWidget(m_pageStack);
    auto* grid = new QGridLayout(page);
    grid->setColumnStretch(0, 1);

    int row = 0;
    for (const std::string& captureName : m_engine.captureNamesFor(language))
    {
        auto* label = new QLabel(QString::fromStdString(captureName), page);
        grid->addWidget(label, row, 0);

        auto* swatchButton = new QPushButton(page);
        swatchButton->setFixedWidth(60);
        grid->addWidget(swatchButton, row, 1);

        m_tokenRows.push_back({ language, captureName, swatchButton });
        connect(swatchButton, &QPushButton::clicked, this,
                [this, language, captureName, swatchButton]() { openColorPicker(language, captureName, swatchButton); });

        ++row;
    }

    grid->setRowStretch(row, 1);

    refreshSwatchColors();
    return page;
}

void SettingsDialog::openColorPicker(Language language, const std::string& captureName, QPushButton* swatchButton)
{
    const QColor initial = m_themeViewModel.tokenColor(language, captureName);
    const QColor chosen = QColorDialog::getColor(initial.isValid() ? initial : QColor(Qt::black), this,
                                                  tr("Choose Color for \"%1\"").arg(QString::fromStdString(captureName)));
    if (!chosen.isValid())
    {
        return;
    }

    m_themeViewModel.setTokenColor(language, captureName, chosen);
    swatchButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(chosen.name()));
}

void SettingsDialog::refreshSwatchColors()
{
    for (const TokenRow& row : m_tokenRows)
    {
        const QColor color = m_themeViewModel.tokenColor(row.language, row.captureName);
        if (color.isValid())
        {
            row.swatchButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name()));
        }
        else
        {
            row.swatchButton->setStyleSheet(QString());
        }
    }
}

QWidget* SettingsDialog::buildFileDecorationsPage()
{
    auto* page = new QWidget(m_pageStack);
    auto* layout = new QVBoxLayout(page);

    m_decorationsTable = new QTableWidget(0, 5, page);
    m_decorationsTable->setObjectName(QStringLiteral("settingsDecorationsTable"));
    m_decorationsTable->setHorizontalHeaderLabels({ tr("Patterns"), tr("Sample"), QString(), QString(), QString() });
    m_decorationsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_decorationsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_decorationsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_decorationsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_decorationsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_decorationsTable->horizontalHeader()->setStretchLastSection(false);
    m_decorationsTable->setColumnWidth(0, 150);
    m_decorationsTable->setColumnWidth(1, 150);
    m_decorationsTable->verticalHeader()->setVisible(false);
    m_decorationsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_decorationsTable->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_decorationsTable, 1);

    connect(m_decorationsTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int /*column*/)
            {
                if (row == 0)
                {
                    editHiddenRule(false);
                }
                else if (row == 1)
                {
                    editHiddenRule(true);
                }
                else
                {
                    editDecorationRule(row - kHiddenRowCount);
                }
            });

    auto* addButton = new QPushButton(tr("Add Rule"), page);
    addButton->setObjectName(QStringLiteral("settingsAddDecorationRuleButton"));
    connect(addButton, &QPushButton::clicked, this, &SettingsDialog::addDecorationRule);
    layout->addWidget(addButton);

    refreshDecorationRows();
    return page;
}

namespace
{
    // Shared by the two fixed Hidden Files/Hidden Folders rows and the general pattern rows --
    // builds the styled "Sample Text" preview widget for a rule (Architecture.md §14.29).
    QLabel* buildDecorationSampleLabel(const FileDecorationRule& rule, QWidget* parent)
    {
        auto* sampleLabel = new QLabel(QObject::tr("Sample Text"), parent);
        QFont font = sampleLabel->font();
        if (rule.fontFamily())
        {
            font.setFamily(QString::fromStdString(*rule.fontFamily()));
        }
        if (rule.fontPointSize())
        {
            font.setPointSize(*rule.fontPointSize());
        }
        font.setBold(rule.bold());
        font.setItalic(rule.italic());
        font.setUnderline(rule.underline());
        font.setStrikeOut(rule.strikeout());
        sampleLabel->setFont(font);
        if (rule.hexColor())
        {
            sampleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromStdString(*rule.hexColor())));
        }
        return sampleLabel;
    }
}

void SettingsDialog::refreshDecorationRows()
{
    m_decorationsTable->setRowCount(kHiddenRowCount + static_cast<int>(m_decorationRules.size()));

    // Rows 0/1: the built-in Hidden Files/Hidden Folders rows -- fixed label in the Patterns
    // column, no Move Up/Move Down/Remove buttons (action cells left empty).
    const std::array<std::pair<QString, const FileDecorationRule*>, kHiddenRowCount> hiddenRows = { {
        { tr("Hidden Files"), &m_hiddenFilesRule },
        { tr("Hidden Folders"), &m_hiddenFoldersRule },
    } };
    for (int row = 0; row < kHiddenRowCount; ++row)
    {
        m_decorationsTable->setItem(row, 0, new QTableWidgetItem(hiddenRows[static_cast<size_t>(row)].first));
        m_decorationsTable->setCellWidget(row, 1, buildDecorationSampleLabel(*hiddenRows[static_cast<size_t>(row)].second, m_decorationsTable));
    }

    for (int i = 0; i < static_cast<int>(m_decorationRules.size()); ++i)
    {
        const int row = kHiddenRowCount + i;
        const FileDecorationRule& rule = m_decorationRules[static_cast<size_t>(i)];

        auto* patternsItem = new QTableWidgetItem(QString::fromStdString(rule.patternsRaw()));
        m_decorationsTable->setItem(row, 0, patternsItem);
        m_decorationsTable->setCellWidget(row, 1, buildDecorationSampleLabel(rule, m_decorationsTable));

        auto* upButton = new QToolButton(m_decorationsTable);
        upButton->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
        upButton->setAutoRaise(true);
        upButton->setToolTip(tr("Move Up"));
        upButton->setEnabled(i > 0);
        connect(upButton, &QToolButton::clicked, this, [this, i]() { moveDecorationRule(i, -1); });
        m_decorationsTable->setCellWidget(row, 2, upButton);

        auto* downButton = new QToolButton(m_decorationsTable);
        downButton->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
        downButton->setAutoRaise(true);
        downButton->setToolTip(tr("Move Down"));
        downButton->setEnabled(i + 1 < static_cast<int>(m_decorationRules.size()));
        connect(downButton, &QToolButton::clicked, this, [this, i]() { moveDecorationRule(i, 1); });
        m_decorationsTable->setCellWidget(row, 3, downButton);

        auto* removeButton = new QToolButton(m_decorationsTable);
        removeButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
        removeButton->setAutoRaise(true);
        removeButton->setToolTip(tr("Remove"));
        connect(removeButton, &QToolButton::clicked, this, [this, i]() { removeDecorationRule(i); });
        m_decorationsTable->setCellWidget(row, 4, removeButton);
    }
}

void SettingsDialog::addDecorationRule()
{
    Result<FileDecorationRule> defaultRule =
        FileDecorationRule::create("*", std::nullopt, std::nullopt, std::nullopt, false, false, false, false);
    if (!defaultRule)
    {
        return;
    }

    m_decorationRules.push_back(std::move(defaultRule).value());
    const auto row = static_cast<size_t>(m_decorationRules.size()) - 1;
    refreshDecorationRows();

    FileDecorationRuleDialog dialog(m_decorationRules[row], this);
    if (dialog.exec() != QDialog::Accepted)
    {
        // Roll back the placeholder rule pushed above -- otherwise it stays in m_decorationRules
        // and gets silently persisted on the next unrelated edit/move/remove.
        m_decorationRules.pop_back();
        refreshDecorationRows();
        return;
    }

    m_decorationRules[row] = dialog.rule();
    commitDecorationRules();
}

void SettingsDialog::editDecorationRule(int row)
{
    if (row < 0 || static_cast<size_t>(row) >= m_decorationRules.size())
    {
        return;
    }

    FileDecorationRuleDialog dialog(m_decorationRules[static_cast<size_t>(row)], this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    m_decorationRules[static_cast<size_t>(row)] = dialog.rule();
    commitDecorationRules();
}

void SettingsDialog::editHiddenRule(bool isDirectory)
{
    FileDecorationRule& target = isDirectory ? m_hiddenFoldersRule : m_hiddenFilesRule;
    const FileDecorationRuleDialog::Kind kind =
        isDirectory ? FileDecorationRuleDialog::Kind::HiddenFolders : FileDecorationRuleDialog::Kind::HiddenFiles;

    FileDecorationRuleDialog dialog(target, this, kind);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    target = dialog.rule();
    if (isDirectory)
    {
        m_fileDecorationsViewModel.setHiddenFoldersRule(target);
    }
    else
    {
        m_fileDecorationsViewModel.setHiddenFilesRule(target);
    }
    refreshDecorationRows();
}

void SettingsDialog::moveDecorationRule(int row, int delta)
{
    const int target = row + delta;
    if (row < 0 || target < 0 || static_cast<size_t>(row) >= m_decorationRules.size() ||
        static_cast<size_t>(target) >= m_decorationRules.size())
    {
        return;
    }

    std::swap(m_decorationRules[static_cast<size_t>(row)], m_decorationRules[static_cast<size_t>(target)]);
    commitDecorationRules();
}

void SettingsDialog::removeDecorationRule(int row)
{
    if (row < 0 || static_cast<size_t>(row) >= m_decorationRules.size())
    {
        return;
    }

    m_decorationRules.erase(m_decorationRules.begin() + row);
    commitDecorationRules();
}

void SettingsDialog::commitDecorationRules()
{
    m_fileDecorationsViewModel.setRules(m_decorationRules);
    refreshDecorationRows();
}

namespace
{
    constexpr std::array<NotificationType, 3> kAllNotificationTypes = { NotificationType::Info, NotificationType::Warning,
                                                                          NotificationType::Error };

    QString notificationTypeLabel(NotificationType type)
    {
        switch (type)
        {
            case NotificationType::Info:
                return QObject::tr("Info");
            case NotificationType::Warning:
                return QObject::tr("Warning");
            case NotificationType::Error:
                return QObject::tr("Error");
        }
        return QString();
    }
}

QWidget* SettingsDialog::buildNotificationsPage()
{
    auto* page = new QWidget(m_pageStack);
    auto* layout = new QVBoxLayout(page);

    auto* table = new QTableWidget(static_cast<int>(kAllNotificationTypes.size()), 3, page);
    table->setObjectName(QStringLiteral("settingsNotificationsTable"));
    table->setHorizontalHeaderLabels({ tr("Type"), tr("Enabled"), tr("Timeout") });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table, 1);

    for (int row = 0; row < static_cast<int>(kAllNotificationTypes.size()); ++row)
    {
        const NotificationType type = kAllNotificationTypes[static_cast<size_t>(row)];
        const NotificationTypeSettings typeSettings = m_notificationSettingsViewModel.settingsFor(type);

        table->setItem(row, 0, new QTableWidgetItem(notificationTypeLabel(type)));

        auto* enabledCheckBox = new QCheckBox(table);
        enabledCheckBox->setObjectName(QStringLiteral("settingsNotificationEnabledCheckBox"));
        enabledCheckBox->setChecked(typeSettings.enabled);
        table->setCellWidget(row, 1, enabledCheckBox);

        auto* timeoutSpinBox = new QSpinBox(table);
        timeoutSpinBox->setObjectName(QStringLiteral("settingsNotificationTimeoutSpinBox"));
        timeoutSpinBox->setRange(0, 3600);
        timeoutSpinBox->setSuffix(tr(" s"));
        timeoutSpinBox->setSpecialValueText(tr("Never"));
        timeoutSpinBox->setValue(typeSettings.timeoutMs / 1000);
        table->setCellWidget(row, 2, timeoutSpinBox);

        // The spin box only offers whole-second granularity, so a sub-second remainder in
        // typeSettings.timeoutMs (e.g. a hand-edited 1500) can't round-trip through it. Track the
        // exact value here and only overwrite it when the timeout box itself is edited, so toggling
        // Enabled alone never truncates a timeout the user didn't touch.
        auto exactTimeoutMs = std::make_shared<int>(typeSettings.timeoutMs);

        connect(enabledCheckBox, &QCheckBox::toggled, this,
                [this, type, exactTimeoutMs](bool checked)
                { m_notificationSettingsViewModel.setSettingsFor(type, checked, *exactTimeoutMs); });
        connect(timeoutSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, type, enabledCheckBox, exactTimeoutMs](int seconds)
                {
                    *exactTimeoutMs = seconds * 1000;
                    m_notificationSettingsViewModel.setSettingsFor(type, enabledCheckBox->isChecked(), *exactTimeoutMs);
                });
    }

    return page;
}

QWidget* SettingsDialog::buildColumnsPage()
{
    auto* page = new QWidget(m_pageStack);
    auto* pageLayout = new QVBoxLayout(page);

    auto* groupBox = new QGroupBox(tr("Default visibility for new tab"), page);
    auto* groupLayout = new QVBoxLayout(groupBox);

    auto* table = new QTableWidget(static_cast<int>(kAllVisibilityColumns.size()), 2, groupBox);
    table->setObjectName(QStringLiteral("settingsColumnsTable"));
    table->setHorizontalHeaderLabels({ tr("Column"), tr("Visible") });
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    groupLayout->addWidget(table);

    for (int row = 0; row < static_cast<int>(kAllVisibilityColumns.size()); ++row)
    {
        const FileListModel::Column column = kAllVisibilityColumns[static_cast<size_t>(row)];

        table->setItem(row, 0, new QTableWidgetItem(columnDisplayName(column)));

        auto* visibleCheckBox = new QCheckBox(table);
        visibleCheckBox->setObjectName(QStringLiteral("settingsColumnVisibleCheckBox"));
        visibleCheckBox->setChecked(m_columnVisibilitySettingsViewModel.defaultVisibility(column));
        table->setCellWidget(row, 1, visibleCheckBox);

        connect(visibleCheckBox, &QCheckBox::toggled, this, [this, column](bool checked) {
            m_columnVisibilitySettingsViewModel.setDefaultVisibility(column, checked);
        });
    }

    pageLayout->addWidget(groupBox);
    pageLayout->addStretch();

    return page;
}
