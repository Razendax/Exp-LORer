# Architecture Document — Feature Decision Log: Text Editor

Continues [Architecture.md](../Architecture.md) §14. Everything here extends the UI/Adapters
layers only — **no Domain or Application layer redesign** — and is recorded as a decision log
per feature, not an implementation walkthrough; see git history and the code's own comments
(many cite these subsection numbers directly) for the detail.

Covers §14.93 — the built-in text editor dialog (`src/ui/widgets/texteditor/`). The editor widget
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
