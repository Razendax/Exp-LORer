#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

// Plain data for FileTransferUseCase (Architecture.md §14.47) -- the custom copy/move pipeline
// behind the progress notification. Qt-free, same posture as every other Application-layer type.

struct FileTransferItem
{
    std::filesystem::path source;      // absolute, real path
    std::filesystem::path destination; // absolute, mapped path under the chosen destination root
    bool isDirectory = false;
    std::uintmax_t size = 0; // 0 for directories
};

struct FileTransferPlan
{
    std::vector<FileTransferItem> items; // directories AND files, any order
    std::size_t totalFileCount = 0;      // regular files only
    std::uintmax_t totalBytes = 0;
};

// Plain callbacks, no Qt -- FileTransferUseCase calls these synchronously on whatever thread
// executeTransfer() runs on (a background QThread via FileTransferWorker). The Qt-facing layer is
// responsible for marshalling to the UI thread, same posture as
// IFileSystemRepository::listDirectoryRecursive's onProgress.
struct FileTransferCallbacks
{
    // Called before starting each file (not directories); the Qt layer derives the "relative path"
    // text for the bottom label from `item`.
    std::function<void(const FileTransferItem& item)> onFileStarted;

    // Cumulative bytes copied so far across the *whole* plan (monotonic), called periodically
    // (throttled inside executeTransfer) and at least once per completed file.
    std::function<void(std::uintmax_t bytesCopiedSoFar)> onBytesProgress;

    // Polled between files and during each file's chunked copy; returning true aborts ASAP.
    std::function<bool()> shouldCancel;
};
