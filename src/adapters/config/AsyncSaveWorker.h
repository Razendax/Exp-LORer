#pragma once

#include <functional>
#include <utility>

#include <QObject>

// Generic replacement for the five otherwise-identical hand-rolled *SaveWorker classes this
// directory used to have (FileDecorationRulesSaveWorker, HighlightThemeSaveWorker,
// FavoritesSaveWorker, NotificationSettingsSaveWorker, ColumnVisibilitySettingsSaveWorker) --
// each just ran one Store::save(T) call on whatever thread it had been moved to. Runs a
// caller-supplied save function instead, so one template covers every value type: disk I/O must
// never run on the Qt UI thread, and this is always used moved onto a dedicated background QThread
// (see AsyncSaveDispatcher, which owns that thread for you).
//
// No Q_OBJECT: a template class can't be moc'd, and this needs none of the meta-object features
// (no signals/slots/Q_PROPERTY of its own) -- QMetaObject::invokeMethod's QObject-context overload
// only needs this to be a QObject for thread affinity, which the plain QObject base already gives.
template <typename T>
class AsyncSaveWorker : public QObject
{
public:
    using SaveFn = std::function<void(const T&)>;

    explicit AsyncSaveWorker(SaveFn saveFn, QObject* parent = nullptr)
        : QObject(parent)
        , m_saveFn(std::move(saveFn))
    {
    }

    void save(const T& value) const { m_saveFn(value); }

private:
    SaveFn m_saveFn;
};
