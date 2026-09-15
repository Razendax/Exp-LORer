#pragma once

#include <gmock/gmock.h>

#include "IContextMenuProvider.h"

class MockContextMenuProvider : public IContextMenuProvider
{
public:
    MOCK_METHOD(Result<void>, showItemContextMenu,
                (const std::vector<std::filesystem::path>& paths, NativeScreenPoint screenPosition, NativeWindowHandle ownerWindow),
                (override));
    MOCK_METHOD(Result<void>, showBackgroundContextMenu,
                (const std::filesystem::path& folder, NativeScreenPoint screenPosition, NativeWindowHandle ownerWindow),
                (override));
};
