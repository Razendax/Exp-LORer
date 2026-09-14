#pragma once

#include <memory>

#include "FileNavigationUseCase.h"
#include "StandardFileSystemRepository.h"

class NavigationViewModel;

// Wires concrete adapters/repositories into use cases via constructor injection. Owns the shared
// adapter/use-case instances; ViewModels are created only through its factory methods
// (Architecture.md §14.6).
class CompositionRoot
{
public:
    CompositionRoot();
    ~CompositionRoot();

    std::unique_ptr<NavigationViewModel> createNavigationViewModel();

private:
    StandardFileSystemRepository m_fileSystemRepository;
    FileNavigationUseCase m_fileNavigationUseCase;
};
