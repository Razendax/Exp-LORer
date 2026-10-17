#pragma once

// Feature toggles for TextEditorWidget/TextEditorPanel/TextEditorDialog, so the editor can be
// embedded with only the features an embedding site needs. All default to true so a
// default-constructed instance reproduces today's fully-featured editor.
struct TextEditorOptions
{
    bool findEnabled = true;
    bool goToLineEnabled = true;
    bool lineNumbersEnabled = true;
    bool multicursorEnabled = true;
};
