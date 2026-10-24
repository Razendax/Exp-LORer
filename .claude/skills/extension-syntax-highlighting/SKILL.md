---
name: extension-syntax-highlighting
description: Checklist for wiring syntax-highlighting ("highlight") support for a file extension in the Preview tab and built-in text editor -- mapping an extension onto an existing Language vs. vendoring a brand-new tree-sitter grammar, and every touch point each path requires (FilePreviewUseCase's text gate, LanguageRegistry, SyntaxHighlightEngine, HighlightThemeStore, SettingsDialog/TextEditorOptionsDialog, vcpkg overlay port, resources/tree-sitter-queries.qrc, tests). Use when asked to "add highlighting for .ext", "support syntax highlighting for <language>", or extend the per-language Colors settings page with a new language. Covers Architecture.md §14.26/§14.100.
---

# Adding Syntax-Highlight Support for a File Extension

This app's text-preview/editor syntax highlighting (Architecture.md §14.26, expanded §14.100) is
a static-table dispatch: `extension -> Language enum -> tree-sitter grammar + highlights.scm
query`. There are two independent gates an extension must pass before it renders highlighted:

1. **Is it previewed as text at all?** `FilePreviewUseCase::isTextExtension()`
   (`src/application/media/FilePreviewUseCase.cpp`) decides this, independently of highlighting.
2. **Does it resolve to a `Language`?** `LanguageRegistry::languageForExtension()`
   (`src/adapters/highlighting/LanguageRegistry.cpp`) decides this. An extension can be
   text-previewed with no `Language` (renders as plain, unhighlighted text) -- see the extension
   lists in `FilePreviewUseCase.cpp` vs. the comment at the top of `LanguageRegistry.h` for exactly
   which extensions are in (1) but not (2) today.

Both gates must pass for highlighting to actually show up. Fix whichever one the target extension
is missing.

## When to Use

- Asked to add syntax highlighting for a new file extension or language in the Preview tab /
  built-in text editor.
- Asked why a given extension's preview isn't highlighted (diagnose which gate above it fails).
- Extending the per-language theme editor (`Settings -> General -> Colors`) or the text editor's
  highlighting combo box with a new language.

## When NOT to Use

- Per-file/folder **color decoration** by name/extension pattern (e.g. "make all `.exe` files
  red") -- that's the separate, already-generic `FileDecorationRule` pattern system
  (Architecture.md §14.30, `docs/features/11-decoration.md`, `src/adapters/decoration`). It already
  supports `*.ext` wildcards with no code change; nothing here applies to it.
- Media (image/video/PDF) extension classification -- that's `MediaExtensions.h`/
  `ArchiveExtensions.h`, unrelated to tree-sitter.

## Step 0: Pick the Path

Does the extension belong to a language **already** in `enum class Language`
(`src/adapters/highlighting/Language.h`: C, Cpp, CSharp, Python, JavaScript, TypeScript, Json,
Html, Css, Yaml, PowerShell, Markdown)? E.g. mapping `.jsx`/`.mjs` onto the existing
`JavaScript` entry, or `.yml` onto `Yaml`, needs no new grammar.

- **Yes -> Case A** (cheap: no grammar, no CMake/vcpkg/resource changes).
- **No, it's a genuinely new language -> Case B** (vendor a new tree-sitter grammar).

## Case A: New Extension, Existing Language

1. `src/application/media/FilePreviewUseCase.cpp` -- add the extension to `isTextExtension()`'s
   `kTextExtensions` array if it isn't already text-previewed (gate 1 above).
2. `src/adapters/highlighting/LanguageRegistry.cpp` -- add an `if (lowercaseExtension == "...")`
   branch in `languageForExtension()` returning the existing `Language::...` value.
3. `src/adapters/highlighting/LanguageRegistry.h` -- update the doc comment's "aren't one of the
   twelve languages here (txt/xml/ini/.../markdown)" list if the extension was named there (it
   enumerates text-but-unhighlighted extensions, so it drifts out of date the moment one of them
   gets a mapping).
4. `tests/adapters/SyntaxHighlightEngineTest.cpp` -- add an `EXPECT_EQ(LanguageRegistry::
   languageForExtension(".ext"), Language::...)` assertion next to the existing ones.

Nothing else changes -- `SyntaxHighlightEngine`, `HighlightThemeStore`, `SettingsDialog`,
`TextEditorOptionsDialog` all key off the `Language` enum value, which already exists.

## Case B: Brand-New Language

Every one of these is required; skipping one leaves the feature silently half-wired (e.g. the
grammar builds but never renders, or renders but can't be themed).

1. **Vendor the grammar** -- `vcpkg-overlays/ports/tree-sitter-<lang>/` (new folder). Copy an
   existing port as a template (e.g. `tree-sitter-yaml/`) rather than writing from scratch:
   - `vcpkg.json` -- `name`/`version`/`description`/`homepage`/`license`, `dependencies: ["tree-sitter", {"name": "vcpkg-cmake", "host": true}]`.
   - `portfile.cmake` -- `vcpkg_from_github` pulling the grammar's release tag + real `SHA512`
     (don't fabricate the hash -- vcpkg verifies it), copies this folder's own `CMakeLists.txt`
     over the upstream one (avoids needing the tree-sitter CLI to regenerate `parser.c`), then
     `vcpkg_cmake_configure`/`install`/`vcpkg_install_copyright`.
   - `CMakeLists.txt` -- compiles the release archive's pre-generated `src/parser.c` (+
     `src/scanner.c` if present, + any extra `src/*.c` the specific grammar ships, e.g. YAML's
     schema variants) as a static lib named `tree-sitter-<lang>`, installs the C header to
     `tree_sitter/`.
   - `vcpkg-configuration.json` (repo root) already points `overlay-ports` at
     `./vcpkg-overlays/ports` -- no change needed there, the new folder is picked up automatically.
2. **`vcpkg.json`** (repo root) -- add `"tree-sitter-<lang>"` to `dependencies`.
3. **`src/adapters/highlighting/Language.h`** -- add the new enum value.
4. **`src/adapters/highlighting/LanguageRegistry.cpp`** -- add the extension(s) ->
   `Language::<New>` branch(es) in `languageForExtension()`, and a `case Language::<New>:` in
   `displayName()` (human-readable name shown in Settings/combo boxes).
5. **`src/adapters/highlighting/SyntaxHighlightEngine.cpp`** -- three additions:
   - `extern "C" const TSLanguage* tree_sitter_<lang>(void);` declaration at the top.
   - A `case Language::<New>: return tree_sitter_<lang>();` in `tsLanguageFor()`.
   - A `case Language::<New>: return QStringLiteral(":/highlighting/<lang>.scm");` in
     `queryResourcePathFor()`.
6. **`resources/tree-sitter-queries/<lang>.scm`** -- the highlights query (adapt the grammar's own
   upstream `queries/highlights.scm` if it ships one). Capture names matter: `HighlightTheme::
   defaultColorFor()` derives its base palette from the capture name's first dot-segment
   (`keyword`, `string`, `comment`, `number`, `function`, `type`), so reuse those names where the
   grammar's node types support it instead of inventing new ones that'll show up uncolored.
7. **`resources/tree-sitter-queries.qrc`** -- add
   `<file alias="<lang>.scm">tree-sitter-queries/<lang>.scm</file>` inside the `/highlighting`
   `qresource`.
8. **`src/adapters/highlighting/CMakeLists.txt`** -- add `<lang>` (the vcpkg port's grammar-name
   suffix, e.g. `c-sharp` not `csharp`/`CSharp`) to the `foreach(grammar IN ITEMS ...)` list so
   `find_library`/`target_link_libraries` picks up the new static lib.
9. **`src/adapters/config/HighlightThemeStore.cpp`** -- add `Language::<New>` to both `toString()`
   and `languageFromString()` (the JSON key used to persist per-token color overrides). Skipping
   this silently drops any user color customization for the new language on save/reload.
10. **`src/ui/widgets/core/SettingsDialog.cpp`** and
    **`src/ui/widgets/texteditor/TextEditorOptionsDialog.cpp`** -- add `Language::<New>` to each
    file's language list array so the new language appears in the per-language theme editor and
    the text editor's highlighting combo box.
11. **`src/application/media/FilePreviewUseCase.cpp`** -- add the extension(s) to
    `isTextExtension()`'s `kTextExtensions` array (gate 1) -- without this the file never reaches
    the highlighter regardless of everything above.
12. **Tests**:
    - `tests/adapters/SyntaxHighlightEngineTest.cpp` -- extend the `languageForExtension` mapping
      assertions, and add a `highlight()` smoke test with a short source snippet asserting at
      least one returned `HighlightSpan` with an expected `captureName`.
    - `tests/adapters/HighlightThemeTest.cpp` -- extend if the new language needs
      default-color/override coverage.
13. **Docs** -- update the extension list called out in `docs/features/05-preview-and-decoration.md`
    §14.26/§14.100 (and `LanguageRegistry.h`'s doc comment, step A.3 above) to mention the newly
    supported language, per this repo's convention of keeping the feature decision log in sync with
    what the code actually supports.

## Not Needed, Either Case

- `HighlightTheme::defaultColorFor()`/`captureNamesFor()` -- both generic over capture-name
  prefixes, nothing language-specific to add.
- `FileIconDelegate`/`FileTileDelegate`/`SearchResultDelegate` -- they read `Qt::ForegroundRole`/
  `Qt::FontRole` generically; unrelated to which `Language` a file resolves to.
- Any `CompositionRoot`/Port/use-case wiring -- this whole stack is adapters+UI only (§14.26's own
  note: "no Domain/Application change, no new Port").
