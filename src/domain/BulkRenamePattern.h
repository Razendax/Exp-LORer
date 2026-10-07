#pragma once

#include <optional>
#include <string>

#include "Result.h"

// Describes one bulk-rename pattern plus its own application logic (Architecture.md §14.55) --
// same free-standing, UTF-8 in/out, no filesystem/Qt deps style as GlobMatch/RegexMatch/
// WholeWordMatch. Stages are applied in a fixed order: find/replace -> case transform -> counter ->
// extension override, matching the conventional order of existing bulk-rename tools. Forbidden-
// character rejection on the result is the use case's job (BulkRenameUseCase), not this struct's --
// this only transforms text.
struct BulkRenamePattern
{
    // Stage 1: find/replace. Ignored (passthrough) when findText is empty.
    std::string findText;
    std::string replaceText;
    bool useRegex = false;  // findText compiled as std::wregex (ECMAScript, same as RegexMatch);
                             // replaceText may use $1.. backreferences via std::regex_replace.
    bool matchCase = false; // case-sensitive match when true; plain mode folds via UnicodeCaseFold.

    // Stage 2: case transform, applied to the result of stage 1.
    enum class CaseTransform
    {
        None,
        Upper,
        Lower,
        Title,
    };
    CaseTransform caseTransform = CaseTransform::None;

    // Stage 3: sequential counter.
    bool counterEnabled = false;
    int counterStart = 1;
    int counterStep = 1;
    int counterDigits = 1; // zero-padded to this width
    enum class CounterPosition
    {
        Prefix,
        Suffix,
    };
    CounterPosition counterPosition = CounterPosition::Suffix;
    std::string counterSeparator; // e.g. "_", inserted between name and counter

    // Stage 4: extension override. nullopt keeps the original extension.
    std::optional<std::string> newExtension;

    // Applies all four stages in order to one item. indexInBatch is 0-based, used only for the
    // counter. Returns the full new filename (stem + extension), UTF-8. Fails only for an invalid
    // regex (std::regex_error at compile time), mirroring RegexMatch::matches's error boundary.
    Result<std::string> apply(const std::string& originalStem, const std::string& originalExtension,
                               int indexInBatch) const;
};
