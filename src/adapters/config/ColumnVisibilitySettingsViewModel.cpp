#include "ColumnVisibilitySettingsViewModel.h"

ColumnVisibilitySettingsViewModel::ColumnVisibilitySettingsViewModel(ColumnVisibilitySettings& settings,
                                                                      ColumnVisibilitySettingsStore& store, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
    , m_saveDispatcher([&store](const ColumnVisibilitySettings& v) { store.save(v); })
{
}

void ColumnVisibilitySettingsViewModel::setDefaultVisibility(FileListModel::Column column, bool visible)
{
    m_settings.visible[static_cast<std::size_t>(column)] = visible;
    emit settingsChanged();
    m_saveDispatcher.dispatch(m_settings);
}
