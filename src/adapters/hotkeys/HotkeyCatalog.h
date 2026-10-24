#pragma once

#include <vector>

#include "HotkeyCatalogEntry.h"

// Hand-written static table of every rebindable action (Architecture.md §14.x) -- one milestone
// appends one block of rows. entries() always returns the same stable storage (a function-local
// static built once), so HotkeyManager may safely keep pointers into it for the process lifetime.
namespace HotkeyCatalog
{
    const std::vector<HotkeyCatalogEntry>& entries();
}
