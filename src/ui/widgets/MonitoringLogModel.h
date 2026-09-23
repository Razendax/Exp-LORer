#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include <QAbstractListModel>
#include <QFileIconProvider>

#include "FolderChangeEvent.h"

// Qt-facing list model backing the Monitoring panel's log view (Architecture.md §14.24). Rebuilt
// wholesale via setEntries() (the displayed tab's whole ring-buffer log, on
// MonitoringPanelViewModel::monitoringLogReset), appended-to incrementally via
// beginInsertRows/beginRemoveRows in appendEntries() (on monitoringLogAppended) so a
// fast-scrolling build doesn't force a full model reset per batch -- mirrors FolderPreviewListModel's
// "only rebuild what changed" posture, but keeps its own running window capped at kCap, mirroring
// TabViewModel's own ring-buffer cap on its monitoring log.
class MonitoringLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit MonitoringLogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

public slots:
    void setEntries(const std::deque<FolderChangeEvent>& entries);
    void appendEntries(const std::vector<FolderChangeEvent>& batch);

private:
    static constexpr std::size_t kCap = 200;

    std::deque<FolderChangeEvent> m_entries;
    QFileIconProvider m_iconProvider;
};
