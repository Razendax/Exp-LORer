#pragma once

#include <QObject>

#include "AsyncSaveDispatcher.h"
#include "ColumnVisibilitySettings.h"
#include "ColumnVisibilitySettingsStore.h"
#include "FileListModel.h"

// Thin Qt-facing wrapper around ColumnVisibilitySettings + ColumnVisibilitySettingsStore, owned
// once by CompositionRoot (same posture as NotificationSettingsViewModel) so Settings > General >
// Columns's "Default visibility for new tab" table is the single editor of this live state.
// WorkspacePaneWidget takes this by reference (not a copied snapshot) and reads it live at each
// tab-creation call site, so a Settings edit after launch affects only tabs created afterward,
// never already-open ones.
class ColumnVisibilitySettingsViewModel : public QObject
{
    Q_OBJECT

public:
    ColumnVisibilitySettingsViewModel(ColumnVisibilitySettings& settings, ColumnVisibilitySettingsStore& store,
                                       QObject* parent = nullptr);

    bool defaultVisibility(FileListModel::Column column) const
    {
        return m_settings.visible[static_cast<std::size_t>(column)];
    }
    void setDefaultVisibility(FileListModel::Column column, bool visible);

signals:
    void settingsChanged();

private:
    ColumnVisibilitySettings& m_settings;
    ColumnVisibilitySettingsStore& m_store;

    AsyncSaveDispatcher<ColumnVisibilitySettings> m_saveDispatcher;
};
