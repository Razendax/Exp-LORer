#include "CompositionRoot.h"

#include "NavigationViewModel.h"

CompositionRoot::CompositionRoot()
    : m_fileNavigationUseCase(m_fileSystemRepository)
{
}

CompositionRoot::~CompositionRoot() = default;

std::unique_ptr<NavigationViewModel> CompositionRoot::createNavigationViewModel()
{
    return std::make_unique<NavigationViewModel>(m_fileNavigationUseCase);
}
