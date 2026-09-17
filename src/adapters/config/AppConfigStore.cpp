#include "AppConfigStore.h"

#include <array>

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

namespace
{
    constexpr int kCurrentVersion = 1;

    QString pathToQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    std::filesystem::path qStringToPath(const QString& text)
    {
        return std::filesystem::path(text.toStdWString());
    }

    QString toString(SplitLayout layout)
    {
        switch (layout)
        {
            case SplitLayout::Single:
                return QStringLiteral("Single");
            case SplitLayout::TwoVertical:
                return QStringLiteral("TwoVertical");
            case SplitLayout::TwoHorizontal:
                return QStringLiteral("TwoHorizontal");
            case SplitLayout::FourGrid:
                return QStringLiteral("FourGrid");
        }
        return QStringLiteral("Single");
    }

    SplitLayout splitLayoutFromString(const QString& text, SplitLayout fallback)
    {
        if (text == QStringLiteral("Single"))
            return SplitLayout::Single;
        if (text == QStringLiteral("TwoVertical"))
            return SplitLayout::TwoVertical;
        if (text == QStringLiteral("TwoHorizontal"))
            return SplitLayout::TwoHorizontal;
        if (text == QStringLiteral("FourGrid"))
            return SplitLayout::FourGrid;
        return fallback;
    }

    QString toString(WorkspacePaneId id)
    {
        switch (id)
        {
            case WorkspacePaneId::PaneA:
                return QStringLiteral("PaneA");
            case WorkspacePaneId::PaneB:
                return QStringLiteral("PaneB");
            case WorkspacePaneId::PaneC:
                return QStringLiteral("PaneC");
            case WorkspacePaneId::PaneD:
                return QStringLiteral("PaneD");
        }
        return QStringLiteral("PaneA");
    }

    WorkspacePaneId workspacePaneIdFromString(const QString& text, WorkspacePaneId fallback)
    {
        if (text == QStringLiteral("PaneA"))
            return WorkspacePaneId::PaneA;
        if (text == QStringLiteral("PaneB"))
            return WorkspacePaneId::PaneB;
        if (text == QStringLiteral("PaneC"))
            return WorkspacePaneId::PaneC;
        if (text == QStringLiteral("PaneD"))
            return WorkspacePaneId::PaneD;
        return fallback;
    }

    QString toString(ViewMode mode)
    {
        switch (mode)
        {
            case ViewMode::ExtraLargeIcons:
                return QStringLiteral("ExtraLargeIcons");
            case ViewMode::LargeIcons:
                return QStringLiteral("LargeIcons");
            case ViewMode::MediumIcons:
                return QStringLiteral("MediumIcons");
            case ViewMode::SmallIcons:
                return QStringLiteral("SmallIcons");
            case ViewMode::List:
                return QStringLiteral("List");
            case ViewMode::Details:
                return QStringLiteral("Details");
            case ViewMode::Tiles:
                return QStringLiteral("Tiles");
        }
        return QStringLiteral("Details");
    }

    ViewMode viewModeFromString(const QString& text, ViewMode fallback)
    {
        if (text == QStringLiteral("ExtraLargeIcons"))
            return ViewMode::ExtraLargeIcons;
        if (text == QStringLiteral("LargeIcons"))
            return ViewMode::LargeIcons;
        if (text == QStringLiteral("MediumIcons"))
            return ViewMode::MediumIcons;
        if (text == QStringLiteral("SmallIcons"))
            return ViewMode::SmallIcons;
        if (text == QStringLiteral("List"))
            return ViewMode::List;
        if (text == QStringLiteral("Details"))
            return ViewMode::Details;
        if (text == QStringLiteral("Tiles"))
            return ViewMode::Tiles;
        return fallback;
    }

    QString toString(SortCriterion criterion)
    {
        switch (criterion)
        {
            case SortCriterion::Name:
                return QStringLiteral("Name");
            case SortCriterion::Size:
                return QStringLiteral("Size");
            case SortCriterion::ModificationDate:
                return QStringLiteral("ModificationDate");
            case SortCriterion::FileType:
                return QStringLiteral("FileType");
        }
        return QStringLiteral("Name");
    }

    SortCriterion sortCriterionFromString(const QString& text, SortCriterion fallback)
    {
        if (text == QStringLiteral("Name"))
            return SortCriterion::Name;
        if (text == QStringLiteral("Size"))
            return SortCriterion::Size;
        if (text == QStringLiteral("ModificationDate"))
            return SortCriterion::ModificationDate;
        if (text == QStringLiteral("FileType"))
            return SortCriterion::FileType;
        return fallback;
    }

    QJsonObject tabConfigToJson(const TabConfig& tab)
    {
        QJsonObject json;
        json[QStringLiteral("path")] = pathToQString(tab.path);
        json[QStringLiteral("viewMode")] = toString(tab.viewMode);
        json[QStringLiteral("sortCriterion")] = toString(tab.sortCriterion);
        json[QStringLiteral("sortAscending")] = tab.sortAscending;
        return json;
    }

    // Returns std::nullopt only when `json` isn't an object at all; a missing/malformed individual
    // field falls back to that field's TabConfig default rather than discarding the whole tab.
    std::optional<TabConfig> tabConfigFromJson(const QJsonValue& json)
    {
        if (!json.isObject())
        {
            return std::nullopt;
        }

        const QJsonObject object = json.toObject();
        TabConfig tab;
        tab.path = qStringToPath(object.value(QStringLiteral("path")).toString());
        tab.viewMode = viewModeFromString(object.value(QStringLiteral("viewMode")).toString(), tab.viewMode);
        tab.sortCriterion = sortCriterionFromString(object.value(QStringLiteral("sortCriterion")).toString(), tab.sortCriterion);
        tab.sortAscending = object.value(QStringLiteral("sortAscending")).toBool(tab.sortAscending);
        return tab;
    }

    QJsonObject paneConfigToJson(const PaneConfig& pane)
    {
        QJsonArray tabs;
        for (const TabConfig& tab : pane.tabs)
        {
            tabs.append(tabConfigToJson(tab));
        }

        QJsonObject json;
        json[QStringLiteral("activeTabIndex")] = pane.activeTabIndex;
        json[QStringLiteral("tabs")] = tabs;
        return json;
    }

    PaneConfig paneConfigFromJson(const QJsonValue& json)
    {
        PaneConfig pane;
        if (!json.isObject())
        {
            return pane;
        }

        const QJsonObject object = json.toObject();
        pane.activeTabIndex = object.value(QStringLiteral("activeTabIndex")).toInt(pane.activeTabIndex);

        for (const QJsonValue& tabValue : object.value(QStringLiteral("tabs")).toArray())
        {
            if (auto tab = tabConfigFromJson(tabValue))
            {
                pane.tabs.push_back(*tab);
            }
        }

        return pane;
    }

    constexpr std::array<WorkspacePaneId, 4> kAllPanes = {
        WorkspacePaneId::PaneA,
        WorkspacePaneId::PaneB,
        WorkspacePaneId::PaneC,
        WorkspacePaneId::PaneD,
    };

    QJsonObject workspaceConfigToJson(const WorkspaceConfig& workspace)
    {
        QJsonObject panes;
        for (WorkspacePaneId id : kAllPanes)
        {
            panes[toString(id)] = paneConfigToJson(workspace.panes[static_cast<size_t>(id)]);
        }

        QJsonObject json;
        json[QStringLiteral("layout")] = toString(workspace.layout);
        json[QStringLiteral("focusedPane")] = toString(workspace.focusedPane);
        json[QStringLiteral("panes")] = panes;
        return json;
    }

    WorkspaceConfig workspaceConfigFromJson(const QJsonValue& json)
    {
        WorkspaceConfig workspace;
        if (!json.isObject())
        {
            return workspace;
        }

        const QJsonObject object = json.toObject();
        workspace.layout = splitLayoutFromString(object.value(QStringLiteral("layout")).toString(), workspace.layout);
        workspace.focusedPane =
            workspacePaneIdFromString(object.value(QStringLiteral("focusedPane")).toString(), workspace.focusedPane);

        const QJsonObject panes = object.value(QStringLiteral("panes")).toObject();
        for (WorkspacePaneId id : kAllPanes)
        {
            workspace.panes[static_cast<size_t>(id)] = paneConfigFromJson(panes.value(toString(id)));
        }

        return workspace;
    }
}

AppConfigStore::AppConfigStore(std::filesystem::path configFilePath)
    : m_configFilePath(std::move(configFilePath))
{
}

AppConfig AppConfigStore::load() const
{
    AppConfig config;

    QFile file(QString::fromStdWString(m_configFilePath.wstring()));
    if (!file.open(QIODevice::ReadOnly))
    {
        return config;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        qWarning() << "AppConfigStore: failed to parse" << file.fileName() << ":" << parseError.errorString();
        return config;
    }

    const QJsonObject root = document.object();
    config.windowGeometry = QByteArray::fromBase64(root.value(QStringLiteral("windowGeometry")).toString().toLatin1());
    config.workspace = workspaceConfigFromJson(root.value(QStringLiteral("workspace")));

    // Tolerant of a missing/malformed/wrong-length array (e.g. an older config.json predating this
    // field, or ColumnCount changing in a future version): any column left unread keeps its
    // default-constructed 0 ("unset").
    const QJsonArray columnWidths = root.value(QStringLiteral("detailsColumnWidths")).toArray();
    for (int i = 0; i < columnWidths.size() && i < static_cast<int>(config.detailsColumnWidths.size()); ++i)
    {
        config.detailsColumnWidths[static_cast<size_t>(i)] = columnWidths.at(i).toInt();
    }

    return config;
}

bool AppConfigStore::save(const AppConfig& config) const
{
    QJsonArray columnWidths;
    for (int width : config.detailsColumnWidths)
    {
        columnWidths.append(width);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kCurrentVersion;
    root[QStringLiteral("windowGeometry")] = QString::fromLatin1(config.windowGeometry.toBase64());
    root[QStringLiteral("workspace")] = workspaceConfigToJson(config.workspace);
    root[QStringLiteral("detailsColumnWidths")] = columnWidths;

    std::error_code errorCode;
    std::filesystem::create_directories(m_configFilePath.parent_path(), errorCode);

    QSaveFile file(QString::fromStdWString(m_configFilePath.wstring()));
    if (!file.open(QIODevice::WriteOnly))
    {
        qWarning() << "AppConfigStore: failed to open" << file.fileName() << "for writing";
        return false;
    }

    file.write(QJsonDocument(root).toJson());

    if (!file.commit())
    {
        qWarning() << "AppConfigStore: failed to save" << file.fileName();
        return false;
    }

    return true;
}
