#include "IconLoadingWorker.h"

#include <cstring>
#include <filesystem>
#include <system_error>

#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#ifdef _WIN32
#include <windows.h>

#include <commctrl.h> // IMAGELISTDRAWPARAMS/IMAGEINFO, which commoncontrols.h below needs but doesn't itself pull in
#include <commoncontrols.h>
#include <objbase.h>
#include <shellapi.h>
#endif

namespace
{
#ifdef _WIN32
    // SHGetFileInfoW (and SHGetImageList/IImageList::GetIcon, which shares the same underlying
    // Shell icon cache) is documented by Microsoft as unsafe to call concurrently from multiple
    // threads: a call can intermittently fail and hand back a null icon handle even though the
    // file/extension is perfectly valid. IconLoadingService's worker pool (WorkerThreadPool.h)
    // runs 2-4 IconLoadingWorker instances on separate QThreads specifically so per-item icon
    // extraction parallelizes -- which is exactly the pattern that triggers this failure mode
    // ("icons sometimes missing for no visible reason, even for files sharing an already-resolved
    // extension"). Serialize every Shell icon-extraction call across the whole pool behind this one
    // process-wide mutex; the pool still parallelizes everything else (cache lookups, GDI
    // compositing into QImage), just not the SHGetFileInfoW/SHGetImageList calls themselves.
    QMutex g_shellIconMutex;
#endif

    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
    }

    QString lowercaseExtensionOf(const FileNode& entry)
    {
        QString ext = toQString(entry.path().extension()).toLower();
        if (!ext.isEmpty() && ext.startsWith(QLatin1Char('.')))
        {
            ext.remove(0, 1);
        }
        return ext;
    }

    // True for entries whose own icon can genuinely differ per instance -- a real directory
    // (desktop.ini/drive overrides) or a file extension the shell resolves from the file's own
    // content/target rather than a generic per-type icon (PE resources for .exe/.dll/.scr/.cpl/
    // .msc, a shortcut's target for .lnk/.url, the image itself for .ico). Everything else shares
    // exactly one icon with every other file of the same extension.
    bool hasPerFileIcon(const FileNode& entry)
    {
        if (entry.isDirectory())
        {
            return true;
        }
        static const QSet<QString> kUniqueIconExtensions = {
            QStringLiteral("exe"), QStringLiteral("dll"), QStringLiteral("lnk"), QStringLiteral("ico"),
            QStringLiteral("scr"), QStringLiteral("cpl"), QStringLiteral("msc"), QStringLiteral("url"),
        };
        return kUniqueIconExtensions.contains(lowercaseExtensionOf(entry));
    }

#ifdef _WIN32
    // Composites `hIcon` onto a 32bpp top-down DIB section via DrawIconEx -- which correctly
    // handles both legacy AND-mask icons and modern per-pixel-alpha icons uniformly, unlike reading
    // the icon's own hbmColor/hbmMask bitmaps directly -- and copies the result into a QImage, a
    // plain data container with no GUI-thread affinity, safe to build on this worker thread and
    // hand across to the GUI thread afterward.
    QImage imageFromHIcon(HICON hIcon, int width, int height)
    {
        if (!hIcon || width <= 0 || height <= 0)
        {
            return {};
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screenDc = GetDC(nullptr);
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screenDc);
        if (!dib || !bits)
        {
            return {};
        }

        std::memset(bits, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

        HDC memDc = CreateCompatibleDC(nullptr);
        HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDc, dib));
        DrawIconEx(memDc, 0, 0, hIcon, width, height, 0, nullptr, DI_NORMAL);
        SelectObject(memDc, oldBitmap);
        DeleteDC(memDc);

        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(image.scanLine(y), static_cast<unsigned char*>(bits) + static_cast<size_t>(y) * static_cast<size_t>(width) * 4,
                         static_cast<size_t>(width) * 4);
        }

        DeleteObject(dib);
        return image;
    }

    // Detects `hIcon`'s real pixel size via GetIconInfo (rather than assuming it matches whatever
    // size was requested -- Shell icon APIs don't guarantee that) and composites it into a QImage.
    // Always destroys `hIcon`, so every caller can treat this as "consume and convert".
    QImage imageFromDetectedHIcon(HICON hIcon)
    {
        if (!hIcon)
        {
            return {};
        }

        int width = 0;
        int height = 0;
        ICONINFO iconInfo{};
        if (GetIconInfo(hIcon, &iconInfo))
        {
            BITMAP bitmap{};
            if (GetObject(iconInfo.hbmColor ? iconInfo.hbmColor : iconInfo.hbmMask, sizeof(bitmap), &bitmap))
            {
                width = bitmap.bmWidth;
                // A legacy monochrome icon stores its AND and XOR masks stacked in one hbmMask
                // bitmap (no hbmColor at all), so its true height is half of that bitmap's height.
                height = iconInfo.hbmColor ? bitmap.bmHeight : bitmap.bmHeight / 2;
            }
            if (iconInfo.hbmColor)
            {
                DeleteObject(iconInfo.hbmColor);
            }
            if (iconInfo.hbmMask)
            {
                DeleteObject(iconInfo.hbmMask);
            }
        }

        const QImage image = imageFromHIcon(hIcon, width, height);
        DestroyIcon(hIcon);
        return image;
    }

    // Resolves `path`'s small (16x16, SHGFI_SMALLICON) system icon -- used only for SmallIcons/
    // List/Details, where that native resolution already matches the paint size exactly.
    QImage smallIconImage(const QString& path, bool useFileAttributes)
    {
        SHFILEINFOW info{};
        UINT flags = SHGFI_ICON | SHGFI_SMALLICON;
        DWORD attributes = FILE_ATTRIBUTE_NORMAL;
        if (useFileAttributes)
        {
            // SHGFI_USEFILEATTRIBUTES asks for the generic icon for this attribute/extension
            // without SHGetFileInfoW itself ever opening the real path -- needed for directories
            // synthesized by archive-browsing (Architecture.md §14.28), which have no real path on
            // disk to stat. Real directories are queried normally (below) so desktop.ini-customized
            // folder icons -- notably "This PC"'s quick-access folders (Desktop/Documents/Pictures/
            // Videos/Music/Downloads, Architecture.md §14.15) and drive icons -- still resolve
            // correctly, matching the synchronous QFileIconProvider lookup this worker replaced.
            flags |= SHGFI_USEFILEATTRIBUTES;
            attributes = FILE_ATTRIBUTE_DIRECTORY;
        }

        BOOL ok = FALSE;
        {
            QMutexLocker locker(&g_shellIconMutex);
            ok = SHGetFileInfoW(reinterpret_cast<const wchar_t*>(path.utf16()), attributes, &info, sizeof(info), flags);
        }
        if (!ok)
        {
            return {};
        }

        return imageFromDetectedHIcon(info.hIcon);
    }

    // Resolves `path`'s "Jumbo" (256x256, SHIL_JUMBO) system icon -- Explorer's own source for
    // Large/Extra Large Icons, and the one used here for every icon-grid view mode (Medium/Large/
    // Extra Large/Tiles). SHGFI_LARGEICON only ever hands back the classic ~32x32 "large" icon, so
    // using it for those modes meant stretching a low-res pixmap up to a 256x256 cell -- blurry,
    // and visibly worse for icons (folders especially) that the Shell otherwise ships a genuine
    // high-resolution version of. Fetching the real 256x256 version here instead means the
    // delegate (FileIconDelegate/FileTileDelegate) only ever scales a row's icon *down* to fit its
    // cell, never up.
    QImage jumboIconImage(const QString& path, bool useFileAttributes)
    {
        SHFILEINFOW info{};
        // SHGFI_SYSICONINDEX asks for the shared system-image-list index rather than an HICON
        // directly -- SHGFI_LARGEICON here only selects which image list the index applies to
        // (required by the flag), not the final resolution; the index itself is valid across
        // every one of Explorer's image lists (small/large/extra-large/jumbo) on Vista+.
        UINT flags = SHGFI_SYSICONINDEX | SHGFI_LARGEICON;
        DWORD attributes = FILE_ATTRIBUTE_NORMAL;
        if (useFileAttributes)
        {
            flags |= SHGFI_USEFILEATTRIBUTES;
            attributes = FILE_ATTRIBUTE_DIRECTORY;
        }

        HICON jumboIcon = nullptr;
        {
            QMutexLocker locker(&g_shellIconMutex);

            if (!SHGetFileInfoW(reinterpret_cast<const wchar_t*>(path.utf16()), attributes, &info, sizeof(info), flags))
            {
                return {};
            }
            if (info.hIcon)
            {
                // Not documented as always empty alongside SHGFI_SYSICONINDEX, so destroy
                // defensively rather than leak it -- this call only wants `info.iIcon`.
                DestroyIcon(info.hIcon);
            }

            IImageList* imageList = nullptr;
            if (FAILED(SHGetImageList(SHIL_JUMBO, IID_IImageList, reinterpret_cast<void**>(&imageList))) || !imageList)
            {
                return {};
            }

            imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &jumboIcon);
            imageList->Release();
        }

        return imageFromDetectedHIcon(jumboIcon);
    }
#endif
}

IconLoadingWorker::IconLoadingWorker(QObject* parent)
    : QObject(parent)
{
}

#ifdef _WIN32
void IconLoadingWorker::ensureComInitialized()
{
    if (m_comInitialized)
    {
        return;
    }

    // RPC_E_CHANGED_MODE (already initialized with a different concurrency model) is harmless here
    // -- it still means some apartment is initialized on this thread, which is all icon extraction
    // needs.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitialized = true;
}

IconImages IconLoadingWorker::resolveImages(const FileNode& entry, bool useFileAttributes, IconSizeMask sizeMask) const
{
    const QString path = toQString(entry.path());

    IconImages images;
    if (hasFlag(sizeMask, IconSizeMask::Small))
    {
        images.smallImage = smallIconImage(path, useFileAttributes);
        images.hasSmall = true;
    }
    if (hasFlag(sizeMask, IconSizeMask::Large))
    {
        images.largeImage = jumboIconImage(path, useFileAttributes);
        images.hasLarge = true;
    }
    return images;
}
#endif

QHash<QString, IconImages> IconLoadingWorker::iconsFor(const std::vector<FileNode>& entries, IconSizeMask sizeMask)
{
    QHash<QString, IconImages> result;
    result.reserve(static_cast<int>(entries.size()));

#ifdef _WIN32
    ensureComInitialized();
#endif

    const bool wantSmall = hasFlag(sizeMask, IconSizeMask::Small);
    const bool wantLarge = hasFlag(sizeMask, IconSizeMask::Large);

    for (const FileNode& entry : entries)
    {
        const QString resultKey = toQString(entry.path());

        // Only a directory with no real path on disk (archive-browsing's synthesized entries,
        // Architecture.md §14.28) needs SHGFI_USEFILEATTRIBUTES -- a real directory is queried
        // normally below so its actual shell icon (desktop.ini overrides, drive icons, ...)
        // resolves.
        std::error_code ec;
        const bool useFileAttributes = entry.isDirectory() && !std::filesystem::exists(entry.path(), ec);

        // Cache key: a constant for every archive-synthesized directory (SHGFI_USEFILEATTRIBUTES
        // never looks at the real path, so they're 100% shareable); the entry's own path for
        // anything hasPerFileIcon() flags (real directories, and files whose icon the shell
        // derives from their own content/target); otherwise one shared key per lowercase
        // extension, since ordinary files of the same extension all show the same generic icon.
        QString cacheKey;
        bool pathBased = false;
        if (useFileAttributes)
        {
            cacheKey = QStringLiteral("@archive-dir");
        }
        else if (hasPerFileIcon(entry))
        {
            cacheKey = resultKey;
            pathBased = true;
        }
        else
        {
            const QString ext = lowercaseExtensionOf(entry);
            cacheKey = ext.isEmpty() ? QStringLiteral("@noext") : QStringLiteral("@ext:") + ext;
        }

        auto cacheIt = m_cache.find(cacheKey);
        const bool staleMtime = pathBased && cacheIt != m_cache.end() && cacheIt.value().modificationDate != entry.modificationDate();
        if (cacheIt == m_cache.end() || staleMtime)
        {
            cacheIt = m_cache.insert(cacheKey, CacheEntry{});
            if (pathBased)
            {
                cacheIt.value().modificationDate = entry.modificationDate();
            }
        }

        IconImages images = cacheIt.value().images;
        const bool needSmall = wantSmall && !images.hasSmall;
        const bool needLarge = wantLarge && !images.hasLarge;

        if (needSmall || needLarge)
        {
#ifdef _WIN32
            const IconSizeMask missingMask = static_cast<IconSizeMask>((needSmall ? static_cast<int>(IconSizeMask::Small) : 0) |
                                                                         (needLarge ? static_cast<int>(IconSizeMask::Large) : 0));
            const IconImages resolved = resolveImages(entry, useFileAttributes, missingMask);
#else
            IconImages resolved;
            resolved.hasSmall = needSmall;
            resolved.hasLarge = needLarge;
#endif
            if (needSmall)
            {
                images.smallImage = resolved.smallImage;
                images.hasSmall = true;
            }
            if (needLarge)
            {
                images.largeImage = resolved.largeImage;
                images.hasLarge = true;
            }
            cacheIt.value().images = images;
        }

        result.insert(resultKey, images);
    }

    return result;
}
