#pragma once

// Opaque native types only, so Application stays framework-agnostic — same boundary precedent as
// std::filesystem::path already crossing into Application (Architecture.md §14.13). Shared by
// IContextMenuProvider and IFileSystemRepository::showProperties.
using NativeWindowHandle = void*;
