#pragma once

#include <gmock/gmock.h>

#include "IContextMenuProvider.h"

class MockContextMenuProvider : public IContextMenuProvider
{
public:
    MOCK_METHOD(Result<std::vector<ContextMenuEntry>>, buildItemMenu,
                (const std::vector<std::filesystem::path>& paths, ContextMenuSourceMode mode), (override));
    MOCK_METHOD(Result<std::vector<ContextMenuEntry>>, buildBackgroundMenu,
                (const std::filesystem::path& folder, ContextMenuSourceMode mode), (override));
    MOCK_METHOD(Result<void>, invoke, (std::uint32_t entryId, NativeWindowHandle ownerWindow), (override));
    MOCK_METHOD(void, discardMenu, (), (override));
};
