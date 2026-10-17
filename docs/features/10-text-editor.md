# Architecture Document — Feature Decision Log: Text Editor

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.93–14.94 — the built-in text editor dialog (`src/ui/widgets/texteditor/`). The editor widget
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
