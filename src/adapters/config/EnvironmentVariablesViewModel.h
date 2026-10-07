#pragma once

#include <vector>

#include <QObject>
#include <QString>

#include "AsyncSaveDispatcher.h"
#include "EnvironmentVariable.h"
#include "EnvironmentVariablesStore.h"

// Thin Qt-facing wrapper around the custom per-process environment variable list +
// EnvironmentVariablesStore (Architecture.md §14.57), owned once by CompositionRoot so the Tools >
// Environment Variables... dialog is the single editor of this live state. Applies the loaded set
// to the real process environment once in the constructor -- before
// CompositionRoot::createWorkspaceController() ever runs, so before any pane/terminal can exist --
// and again on every save(), via EnvironmentVariableApplier. The JSON write itself runs on a
// dedicated background thread (via m_saveDispatcher), same as every other store/ViewModel pair
// here, so it never blocks the Qt UI thread on disk I/O.
//
// Deliberately no granular add/remove/move mutators the way FavoritesViewModel has: the
// Environment Variables dialog is transactional (explicit OK/Cancel), so it keeps its own working
// copy and calls save() exactly once, on OK.
class EnvironmentVariablesViewModel : public QObject
{
    Q_OBJECT

public:
    EnvironmentVariablesViewModel(std::vector<EnvironmentVariable>& variables, EnvironmentVariablesStore& store,
                                   QObject* parent = nullptr);

    const std::vector<EnvironmentVariable>& variables() const noexcept { return m_variables; }

    void save(std::vector<EnvironmentVariable> variables);

signals:
    void environmentVariablesChanged();

private:
    std::vector<QString> currentNames() const;

    std::vector<EnvironmentVariable>& m_variables;
    EnvironmentVariablesStore& m_store;

    AsyncSaveDispatcher<std::vector<EnvironmentVariable>> m_saveDispatcher;
};
