; Reuses the existing six-category base palette (keyword/string/comment/number/function/type --
; HighlightTheme::defaultColorFor, shared uniformly across every language) instead of the
; "text"/"punctuation" capture names tree-sitter's own convention would suggest, so Markdown is
; visibly highlighted out of the box without adding Markdown-only entries to that shared palette
; (which would mean guessing colors for the many other languages whose own highlights.scm already
; emit those same category names, e.g. C/C++/CSS/JS/Python/TS's "@punctuation.*"/"@property").

(atx_heading
  (inline) @keyword)

(setext_heading
  (paragraph) @keyword)

[
  (atx_h1_marker)
  (atx_h2_marker)
  (atx_h3_marker)
  (atx_h4_marker)
  (atx_h5_marker)
  (atx_h6_marker)
  (setext_h1_underline)
  (setext_h2_underline)
] @comment

[
  (indented_code_block)
  (fenced_code_block)
] @string

(fenced_code_block_delimiter) @comment

(link_destination) @string

(link_label) @type

[
  (list_marker_plus)
  (list_marker_minus)
  (list_marker_star)
  (list_marker_dot)
  (list_marker_parenthesis)
  (thematic_break)
] @comment

(block_quote_marker) @comment

(backslash_escape) @string.escape
