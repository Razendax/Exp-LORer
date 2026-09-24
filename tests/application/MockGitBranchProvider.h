#pragma once

#include <gmock/gmock.h>

#include "IGitBranchProvider.h"

class MockGitBranchProvider : public IGitBranchProvider
{
public:
    MOCK_METHOD(Result<GitRepositorySummary>, repositorySummary, (const std::filesystem::path& directory), (const, override));
    MOCK_METHOD(Result<std::vector<std::string>>, listLocalBranches, (const std::filesystem::path& directory), (const, override));
    MOCK_METHOD(Result<void>, checkoutBranch, (const std::filesystem::path& directory, const std::string& branchName),
                (const, override));
};
