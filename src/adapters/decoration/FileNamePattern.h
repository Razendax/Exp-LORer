#pragma once

#include <string>

// One already-unquoted pattern token from a FileDecorationRule's pattern list (Architecture.md
// §14.29). Parses its own '#' folder-marker prefix and matches the remainder as a `*`/`?` glob
// against a UTF-8 file/folder name, decoded to Unicode codepoints internally so `?` consumes one
// *character*, not one UTF-8 byte, and folded ASCII-case-insensitively (non-ASCII case folding is
// out of scope, matching Windows filename semantics closely enough for this feature's purpose).
class FileNamePattern
{
public:
    // `pattern` is one token as produced by FileDecorationRule's comma/quote splitter -- already
    // unquoted, but still carrying a leading '#' if the user wrote one.
    explicit FileNamePattern(std::string pattern);

    bool appliesToFolders() const noexcept { return m_appliesToFolders; }

    // `nameUtf8` is a bare file/folder name (not a path), UTF-8 encoded. Returns false immediately
    // if `isDirectory` disagrees with appliesToFolders() -- a files-only pattern never matches a
    // folder and vice versa -- otherwise glob-matches nameUtf8 against the pattern.
    bool matches(const std::string& nameUtf8, bool isDirectory) const;

private:
    bool m_appliesToFolders = false;
    std::string m_glob;
};
