#pragma once

#include <optional>

#include "Language.h"

// Feature toggles for TextEditorWidget/TextEditorPanel/TextEditorDialog, so the editor can be
// embedded with only the features an embedding site needs. findEnabled/goToLineEnabled/
// lineNumbersEnabled/multicursorEnabled/highlightSelectionOccurrences default to true so a
// default-constructed instance reproduces today's fully-featured editor; readOnly/highlightLanguage
// default to "off" (editable, unhighlighted), matching the editor's pre-existing behavior before
// those two were added.
struct TextEditorOptions
{
    bool findEnabled = true;
    bool goToLineEnabled = true;
    bool lineNumbersEnabled = true;
    bool multicursorEnabled = true;
    bool highlightSelectionOccurrences = true;
    bool readOnly = false;
    std::optional<Language> highlightLanguage = std::nullopt;
};
