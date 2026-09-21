#pragma once

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <string>
#include <vector>

// Test-only helper: authors a small zip fixture on disk via libarchive's own write API, so the
// archive adapter tests don't need a checked-in binary fixture for a write-capable format
// (Architecture.md §11 -- only RAR needs a checked-in fixture, since libarchive can't author it).
namespace ArchiveFixture
{
    struct FileSpec
    {
        std::string relativePath; // '/'-separated, relative to the archive root
        std::string content;
    };

    inline void writeZip(const std::filesystem::path& archivePath, const std::vector<FileSpec>& files)
    {
        archive* a = archive_write_new();
        archive_write_set_format_zip(a);
        archive_write_open_filename_w(a, archivePath.c_str());

        for (const auto& file : files)
        {
            archive_entry* entry = archive_entry_new();
            archive_entry_set_pathname(entry, file.relativePath.c_str());
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, static_cast<la_int64_t>(file.content.size()));
            archive_write_header(a, entry);
            archive_write_data(a, file.content.data(), file.content.size());
            archive_entry_free(entry);
        }

        archive_write_close(a);
        archive_write_free(a);
    }
}
