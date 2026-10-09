#include "IconLoadingWorker.h"

#include <cstring>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <windows.h>

#include <objbase.h>
#include <shellapi.h>
#endif

namespace
{
    QString toQString(const std::filesystem::path& path)
    {
        return QString::fromStdWString(path.wstring());
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

    // One SHGetFileInfoW + DrawIconEx round trip for a single icon size (SHGFI_SMALLICON or
    // SHGFI_LARGEICON).
    QImage iconImage(const QString& path, bool useFileAttributes, UINT sizeFlag)
    {
        SHFILEINFOW info{};
        UINT flags = SHGFI_ICON | sizeFlag;
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

        if (!SHGetFileInfoW(reinterpret_cast<const wchar_t*>(path.utf16()), attributes, &info, sizeof(info), flags) || !info.hIcon)
        {
            return {};
        }

        int width = 0;
        int height = 0;
        ICONINFO iconInfo{};
        if (GetIconInfo(info.hIcon, &iconInfo))
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

        const QImage image = imageFromHIcon(info.hIcon, width, height);
        DestroyIcon(info.hIcon);
        return image;
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

IconImages IconLoadingWorker::resolveImages(const FileNode& entry) const
{
    const QString path = toQString(entry.path());

    // Only a directory with no real path on disk (archive-browsing's synthesized entries,
    // Architecture.md §14.28) needs SHGFI_USEFILEATTRIBUTES -- a real directory is queried
    // normally below so its actual shell icon (desktop.ini overrides, drive icons, ...) resolves.
    std::error_code ec;
    const bool useFileAttributes = entry.isDirectory() && !std::filesystem::exists(entry.path(), ec);

    IconImages images;
    images.smallImage = iconImage(path, useFileAttributes, SHGFI_SMALLICON);
    images.largeImage = iconImage(path, useFileAttributes, SHGFI_LARGEICON);
    return images;
}
#endif

QHash<QString, IconImages> IconLoadingWorker::iconsFor(const std::vector<FileNode>& entries)
{
    QHash<QString, IconImages> result;
    result.reserve(static_cast<int>(entries.size()));

#ifdef _WIN32
    ensureComInitialized();
#endif

    for (const FileNode& entry : entries)
    {
        const QString key = toQString(entry.path());

        const auto cached = m_cache.constFind(key);
        if (cached != m_cache.constEnd() && cached.value().modificationDate == entry.modificationDate())
        {
            result.insert(key, cached.value().images);
            continue;
        }

#ifdef _WIN32
        IconImages images = resolveImages(entry);
#else
        IconImages images;
#endif

        m_cache.insert(key, CacheEntry{ entry.modificationDate(), images });
        result.insert(key, images);
    }

    return result;
}
