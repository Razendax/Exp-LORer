#pragma once

#include <string>
#include <vector>

#include "FileNode.h"

// Plain entities for Content Search ("Find in Files", Architecture.md §14.104). No Qt -- same
// posture as FileNode/FilePreview.

// One matching line within a file. 1-based, matching the editor's own line numbering. No
// match-span data -- highlighting is re-derived in the UI layer at paint time, the same
// precedent SearchResultDelegate already sets for filename search.
struct LineMatch
{
    int lineNumber = 0;
    std::string textUtf8;
};

// One file's content, split into lines, before the main query has been applied. Carries the full
// FileNode (not just a path) so the UI layer can hand it straight to
// TabViewModel::setSelectedEntries on single-click, the same reason SearchPanelWidget keeps its
// own last-results list as full FileNodes rather than paths.
struct RawFileLines
{
    FileNode node;
    std::vector<std::string> lines;
};

// One file's lines that matched the main query.
struct FileContentMatch
{
    FileNode node;
    std::vector<LineMatch> lines;
};
