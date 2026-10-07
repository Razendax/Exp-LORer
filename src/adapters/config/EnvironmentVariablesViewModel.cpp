#include "EnvironmentVariablesViewModel.h"

#include <utility>

#include "EnvironmentVariableApplier.h"

EnvironmentVariablesViewModel::EnvironmentVariablesViewModel(std::vector<EnvironmentVariable>& variables,
                                                               EnvironmentVariablesStore& store, QObject* parent)
    : QObject(parent)
    , m_variables(variables)
    , m_store(store)
    , m_saveDispatcher([&store](const std::vector<EnvironmentVariable>& v) { store.save(v); })
{
    EnvironmentVariableApplier::apply(m_variables, {});
}

void EnvironmentVariablesViewModel::save(std::vector<EnvironmentVariable> variables)
{
    const std::vector<QString> previouslyAppliedNames = currentNames();

    m_variables = std::move(variables);
    EnvironmentVariableApplier::apply(m_variables, previouslyAppliedNames);

    emit environmentVariablesChanged();
    m_saveDispatcher.dispatch(m_variables);
}

std::vector<QString> EnvironmentVariablesViewModel::currentNames() const
{
    std::vector<QString> names;
    names.reserve(m_variables.size());
    for (const EnvironmentVariable& variable : m_variables)
    {
        names.push_back(variable.name);
    }
    return names;
}
