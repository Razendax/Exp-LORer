#pragma once

#include <gmock/gmock.h>

#include "IContextMenuProvider.h"

class MockContextMenuProvider : public IContextMenuProvider
{
public:
    MOCK_METHOD(Result<ContextMenuResult>, showItemMenu,
                (const std::vector<std::filesystem::path>& paths, const NativeMenuLayout& layout, NativeWindowHandle ownerWindow,
                 NativeMenuPoint screenPos),
                (override));
    MOCK_METHOD(Result<ContextMenuResult>, showBackgroundMenu,
                (const std::filesystem::path& folder, const NativeMenuLayout& layout, NativeWindowHandle ownerWindow,
                 NativeMenuPoint screenPos),
                (override));
};
