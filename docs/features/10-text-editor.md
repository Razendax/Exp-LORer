# Architecture Document — Feature Decision Log: Text Editor

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.93–14.97 — the built-in text editor dialog (`src/ui/widgets/texteditor/`). The editor widget
itself (multi-cursor editing, line-number gutter, the Find bar's search/highlight mechanics, Go to
Line) predates this file and shipped undocumented; this file starts from the Find bar's Replace
addition and is where later text-editor features should be logged going forward.

---
### 14.93 Find Bar Replace

`TextEditorFindBar` grows a collapsible Replace row (toggled by a button to the left of the Find
field) with a replace `QLineEdit` plus Replace/Replace All buttons, reusing the bar's existing
floating-overlay styling and the `runSearch` match list rather than introducing a parallel search
path. Enter and Ctrl+Alt+Enter on the replace field are intercepted in that field's installed
`eventFilter` (same precedent as `TextEditorGotoLineBar`'s Return handling) rather than via
`QShortcut`/`returnPressed`, since letting Return bubble back to `QLineEdit`'s own handling would
otherwise reach the editor and insert a newline.

Capture-group references in the replacement text use `$1`, `$2`, … (familiar from VS Code/JS)
rather than Qt's own `\1`, `\2`, … backreference syntax for `QString::replace(QRegularExpression,
QString)` — a small character-scanning translator (not a nested regex substitution, to avoid
double-escaping bugs) converts `$N` → `\N` and `$$` → a literal `$`, and doubles any literal
backslash the user typed so it can't be misread as a Qt backreference. This only applies when
Match Regex is on; literal/plain replacement text is used as-is. Per-match substitution re-runs
the translated regex against just that match's own selected text
(`matchedText.replace(regex, translated)`) instead of carrying a `QRegularExpressionMatch` through
from the original find pass — simple and correct for ordinary capture groups, but a known
limitation: lookaround/boundary assertions that depend on context outside the match span may not
evaluate identically to matching against the full document.

Replace All reuses the same live `QTextCursor` match list `runSearch` already built for
highlighting, and wraps the whole loop in one `beginEditBlock()`/`endEditBlock()` pair (mirroring
the undo-grouping convention `TextEditorWidget`'s multi-cursor mutators already use for
`insertAtAllCursors`/`deleteAtAllCursors`/etc.) so all replacements undo as a single step. Because
those cursors are live and tied to the document, Qt's own cursor-position tracking absorbs the
length changes from earlier replacements in the same pass — no manual offset bookkeeping needed,
the same reasoning that already lets `copyOrCutAtAllCursors`/`pasteAtAllCursors` process multiple
live cursors safely.

---
### 14.94 Find Bar Select All Occurrences

Alt+Enter in the search field (or a new "All" button in the find row, between Next and Close)
turns every current match into a cursor in the editor — `TextEditorFindBar::m_matches`'s first
entry becomes the primary cursor/selection, the rest become secondary cursors via a new
`TextEditorWidget::selectAllOccurrences` — mirroring VS Code's own Find-widget shortcut for the
same action. This reuses the multi-cursor typing path (`insertAtAllCursors`/`deleteAtAllCursors`)
that already exists for Ctrl+click/drag secondary cursors, rather than introducing a parallel
"replace at N cursors" mechanism: once the matches become cursors, typing to replace every
occurrence falls out of machinery that was already there.

Alt+Enter was free to claim: Ctrl+Alt+Enter already triggers Replace All from the replace field,
so Alt+Enter alone (without Ctrl) on the *search* field doesn't collide with it, and nothing at
the `TextEditorDialog` level claims it either. The find highlights and current-match color are
cleared via the existing `hideAndClear()` path immediately afterward, same as closing the bar any
other way — the secondary cursors it just created are untouched by that call, since
`hideAndClear` only ever clears `m_findSelections`, never `m_secondaryCursors`.

A plain text "All" button label was chosen over a Unicode glyph: no single character reliably
reads as "multi-cursor" at toolbutton size (VS Code itself uses a bespoke icon here, not a font
glyph), and this matches the bar's own existing convention of short text labels (`Aa`, `ab`,
`.*`) over icons for ambiguous concepts.

---
### 14.95 Configurable Editor Features + Options Dialog

The editor widget stack was built for exactly one call site (`Tools > Text Editor...`) with every
feature always on. Since the editor is going to be embedded elsewhere in the app for different
purposes going forward, each of its four features — Find, Go to Line, line numbers, multicursor —
needed to be independently switchable off, and a way to exercise every combination from the UI
before any other embedding site exists to prove it out.

A single plain struct, `TextEditorOptions` (`src/ui/widgets/texteditor/TextEditorOptions.h`), holds
the four `bool` flags (all defaulting to `true`, so a default-constructed instance reproduces the
pre-existing fully-featured editor) and threads through the stack as a constructor parameter —
`TextEditorWidget`, `TextEditorFindBar`, `TextEditorPanel`, `TextEditorDialog` — rather than as a
persisted `AppConfig` entry, mirroring `TextEditorDialog`'s own already-transient, non-persisted
nature. Each flag gates the specific pre-existing code path for that feature instead of
introducing a parallel disabled-state mechanism:

- `lineNumbersEnabled` gates whether `TextEditorWidget` ever constructs its
  `TextEditorLineNumberArea` companion; `lineNumberAreaWidth()` returns `0` up front when it
  wasn't, which collapses the gutter to zero width through the pre-existing
  `setViewportMargins`/`resizeEvent` plumbing rather than a separate "hidden" path.
- `multicursorEnabled` gates `TextEditorWidget`'s two secondary-cursor creation entry points
  (Ctrl+Click in `mousePressEvent`, Ctrl+Alt+Up/Down in `keyPressEvent`), and separately gates
  `TextEditorFindBar`'s "All" button visibility and its Alt+Enter shortcut
  (`selectAllOccurrences`'s only other caller) — with both call sites gated, no change was needed
  inside `TextEditorWidget::selectAllOccurrences` itself.
- `findEnabled`/`goToLineEnabled` each gate whether `TextEditorPanel` constructs that bar and its
  `QShortcut` at all; the panel's existing "opening one bar closes the other" handlers (for
  Ctrl+F/Ctrl+G) each null-check the other bar before calling `hideAndClear()` on it, since it may
  simply not exist.

`Tools > Text Editor...` now opens a new `TextEditorOptionsDialog` first — four checkboxes (all
checked by default) plus an Ok button relabeled "Open Editor" — before `MainWindow` constructs
`TextEditorDialog` with the resulting `TextEditorOptions`. The options dialog deliberately has no
knowledge of `TextEditorDialog`; it only collects and returns a `TextEditorOptions` via a getter,
keeping the two dialogs as decoupled as `TextEditorFindBar`/`TextEditorGotoLineBar` already are
from each other.

---
### 14.96 Read-Only Mode + Syntax Highlighting

Two more independently-switchable `TextEditorOptions` flags, following §14.95's established
pattern: `readOnly` (`bool`, default `false`) and `highlightLanguage`
(`std::optional<Language>`, default `std::nullopt`).

**Read-only.** `TextEditorWidget` calls `setReadOnly(options.readOnly)` in its constructor, which
is enough for `QPlainTextEdit`'s own single-cursor path (typing/paste/cut already no-op when
`isReadOnly()`). The multi-cursor path bypasses `QPlainTextEdit`'s own editing entirely, though, so
`handleMultiCursorKeyPress`'s mutating branches (Backspace, Delete, Return/Enter, Tab, Ctrl+X, and
the trailing "insert printable text" fallback) each gained an `if (!m_readOnly)` guard around the
mutation while still returning `true` (swallowing the keystroke, matching `QPlainTextEdit`'s own
read-only behavior of consuming the key without inserting). Navigation, Ctrl+C, Ctrl+Click/
Ctrl+Alt+Up/Down secondary-cursor creation, and Select All Occurrences are deliberately
unguarded — read-only means "no edits," not "no cursors."

`TextEditorFindBar` mutates the document directly via `QTextCursor::insertText` in
`onReplace`/`onReplaceAll`, bypassing `TextEditorWidget` (and so its read-only guard) entirely, so
the find bar needed its own `readOnly` constructor parameter (default `false`, threaded through
from `TextEditorPanel`): the Replace row's toggle button is hidden outright (`setVisible(!readOnly)`
at construction, same gating `multicursorEnabled` already uses for `m_selectAllOccurrencesButton`),
and `onReplace`/`onReplaceAll` each still check `m_readOnly` defensively — belt-and-suspenders
against any path that could reach them despite the row being hidden (there currently is none, since
the row's own Ctrl+Alt+Enter shortcut only fires once the row is visible), rather than relying
solely on the UI being hidden.

**Syntax highlighting.** Rather than building a parallel highlighter for the editor, this reuses
the stack already built for the Preview panel (§14.26): `SyntaxHighlightEngine` +
`HighlightThemeViewModel` + `LanguageRegistry` + the shared `SyntaxHighlighter`
(`QSyntaxHighlighter`) in `src/ui/widgets/preview/`. `TextEditorWidget`'s constructor gained two
optional pointer parameters — `SyntaxHighlightEngine*` and `HighlightThemeViewModel*`, both
defaulting to `nullptr` since, unlike `TextPreviewWidget` (which always has them), a given
`TextEditorWidget` embedding may not request highlighting at all. When
`options.highlightLanguage` has a value and both pointers are non-null, the widget constructs its
own `SyntaxHighlighter` on `document()` and calls `setLanguage(*options.highlightLanguage)` once;
a requested language without the engine/theme to render it is treated as "highlighting
unavailable," not an error. `TextEditorPanel`/`TextEditorDialog` thread the same two pointers
straight through with no opinion of their own, and `MainWindow::showTextEditorDialog` passes its
own already-existing `m_syntaxHighlightEngine`/`m_highlightThemeViewModel` members.

`SyntaxHighlighter` itself gained live-edit support, since its header previously documented preview
text as "never live-edited" and computed spans once per `setLanguage()` call on that assumption.
A new `m_liveEditDebounceTimer` (150ms single-shot, same convention as
`TextEditorFindBar::m_searchDebounceTimer`) is wired to `document()`'s `QTextDocument::
contentsChange` signal in the constructor; the connection's lambda only (re)starts the timer while
`m_language` is set, and the timer's timeout calls `recomputeSpans(); rehighlight();` — identical
to what `setLanguage()` already does on a load. `TextPreviewWidget`'s own use is unaffected: its
document is never edited after `setPreview()` (blocked via `document()->blockSignals(true)` around
the one `setPlainText()` call that would otherwise fire it), so `contentsChange` simply never
fires there, and the new connection is dormant.

Known limitation, accepted for v1 rather than engineered away up front (matching the project's
existing posture toward documented limitations, e.g. §14.93's lookaround caveat):
`SyntaxHighlightEngine::highlight()` re-parses the whole document from scratch on every call, by
its own header comment's design. For the editor this now happens on a 150ms debounce per edit
burst instead of once per file load — acceptable at today's scale, but something an incremental-
parsing pass would need to revisit if large-file editing becomes a real use case.

`LanguageRegistry` gained a `displayName(Language)` helper (`SettingsDialog.cpp` previously kept a
private copy of this exact switch statement for its own per-language theme list), so
`TextEditorOptionsDialog`'s new highlighting `QComboBox` ("Plain Text (no highlighting)" plus the
nine `Language` values) and `SettingsDialog` both call the one shared mapping instead of keeping
two copies in sync.

`TextEditorOptionsDialog` gained a "Read-only" checkbox (default unchecked) and the language combo
box (default "Plain Text") alongside the four pre-existing checkboxes; `options()` sets
`TextEditorOptions::readOnly` and `highlightLanguage` from them, same pattern as the four flags
§14.95 already wires up.

Both flags stay construction-time settings, consistent with how the other four already work. The
Preview-panel-integration ToDo item (replacing the Preview panel's text view with this editor, with
an "Edit" button flipping an *already-open* editor between read-only and editable) will need to
upgrade `readOnly` to a runtime setter at that point — out of scope here, since today's only call
site (`Tools > Text Editor...`) always constructs a fresh editor per `TextEditorOptionsDialog`
choice.

---
### 14.97 Runtime Read-Only Toggle

Closes §14.96's own ToDo note above: `readOnly` upgrades from construction-time-only to a real
runtime toggle, the groundwork the Preview-panel integration (§14.98,
`docs/features/05-preview-and-decoration.md`) needed to flip an *already-open* editor between
read-only and editable rather than tearing down and reconstructing it for that alone (the panel
integration still rebuilds the whole `TextEditorPanel` per previewed file for other reasons — a
clean slate with no leftover find-bar/cursor/undo state — but no longer *needs* to for a read-only
flip specifically).

- `TextEditorWidget::setReadOnly(bool)` (new, shadows `QPlainTextEdit::setReadOnly` by name, not by
  override — the base method isn't virtual): calls the inherited `QPlainTextEdit::setReadOnly()`
  *and* updates `m_readOnly` together. Calling the inherited setter alone would leave `m_readOnly`
  stale, since `handleMultiCursorKeyPress`'s mutating branches (Backspace, Delete, Return/Enter,
  Tab, Ctrl+X, the printable-text fallback — the same set §14.96 originally guarded) read
  `m_readOnly` directly rather than consulting `QPlainTextEdit::isReadOnly()` on every keystroke.
- `TextEditorFindBar::setReadOnly(bool)` (new): toggles `m_toggleReplaceButton`'s visibility to
  match, mirroring the constructor's existing one-time `setVisible(!readOnly)` call. Flipping to
  read-only while the Replace row is already expanded also collapses it
  (`m_toggleReplaceButton->setChecked(false)`, which `onToggleReplaceRow`'s existing handler already
  hides/repositions for) rather than leaving an now-inert expanded row on screen.
- `TextEditorPanel::setReadOnly(bool)` (new): forwards to the editor and, if present, the find bar.
  `TextEditorPanel::editor()` (new, previously private with no getter) lets an embedding site drive
  the editor directly — `toPlainText()`/`setPlainText()`/`document()->isModified()`/
  `document()->modificationChanged` — without this panel growing a forwarding method for each one.
- `TextEditorDialog` / `Tools > Text Editor...`: untouched, same as every prior increment in this
  file — this only adds a capability, read-only editors opened from there still never flip.
