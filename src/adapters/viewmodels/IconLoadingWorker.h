#pragma once

#include <chrono>
#include <vector>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>

#include "FileNode.h"

// Both common icon sizes SHGetFileInfoW can hand back for a file/folder, combined by the GUI
// thread into one QIcon with two pixmaps once delivered, so a QIcon::pixmap(size) request scales
// well regardless of which view mode (Details/List's small icon vs. Large Icons') is asking.
struct IconImages
{
    // Not named `small`/`large` -- <rpcndr.h> (pulled in transitively by <windows.h>) #defines
    // `small` to `char` for old MIDL compatibility, which silently breaks a member of that name.
    QImage smallImage;
    QImage largeImage;
};

// Resolves file/folder icons off the Qt UI thread ("Fix slow folder-open for executable-heavy
// directories") via raw Win32 Shell/GDI calls (SHGetFileInfoW + DrawIconEx) rather than
// QFileIconProvider -- QFileIconProvider is a Qt Widgets class with GUI-thread affinity (it
// crashes without a QGuiApplication/QApplication already constructed, e.g. under the adapter test
// suite's plain QCoreApplication, and Qt's own threading rules don't guarantee it's safe to call
// from a background thread even when a real QApplication does exist) and this worker must run
// entirely off the GUI thread to avoid blocking it on an executable-heavy folder's per-file
// PE-resource icon extraction. Only raw pixel data (QImage, no GUI-thread affinity) crosses back to
// the GUI thread, which is the one to build the actual QIcon/QPixmap.
//
// Owns an in-memory cache keyed by (absolute path, mtime), so revisiting a directory or switching
// tabs doesn't re-extract an already-known icon. Owned and driven by IconLoadingService, same
// shape as GitStatusWorker/FragmentationWorker.
class IconLoadingWorker : public QObject
{
    Q_OBJECT

public:
    explicit IconLoadingWorker(QObject* parent = nullptr);

    // Resolves (or returns the already-cached) icon images for every entry in `entries`, keyed by
    // each entry's absolute path.
    QHash<QString, IconImages> iconsFor(const std::vector<FileNode>& entries);

private:
    struct CacheEntry
    {
        std::chrono::system_clock::time_point modificationDate;
        IconImages images;
    };

#ifdef _WIN32
    // SHGetFileInfoW needs COM explicitly initialized on the calling thread for icon extraction
    // (notably .exe/.dll/.lnk's own PE-resource icons, which go through a shell extension) and
    // crashes instead of just failing if it isn't -- unlike ShellExecuteW/SHFileOperationW
    // elsewhere in this codebase. This dedicated worker thread never gets Qt's usual
    // auto-initialized COM the way the GUI thread does, so iconsFor() lazily initializes it once,
    // the first time it actually runs on this thread.
    void ensureComInitialized();
    bool m_comInitialized = false;

    IconImages resolveImages(const FileNode& entry) const;
#endif

    QHash<QString, CacheEntry> m_cache;
};
