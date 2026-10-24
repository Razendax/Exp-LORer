[
  (bracket_comment)
  (line_comment)
] @comment

[
  (quoted_argument)
  (bracket_argument)
] @string

(escape_sequence) @string.escape

(variable) @variable

(normal_command
  (identifier) @function)

(function_command
  (function)
  (argument_list
    .
    (argument) @function))

(macro_command
  (macro)
  (argument_list
    .
    (argument) @function))

[
  (if)
  (elseif)
  (else)
  (endif)
  (foreach)
  (endforeach)
  (while)
  (endwhile)
  (function)
  (endfunction)
  (macro)
  (endmacro)
  (block)
  (endblock)
] @keyword

[
  "("
  ")"
] @punctuation.bracket

[
  "$"
  "{"
  "}"
] @punctuation.special

[
  "ENV"
  "CACHE"
] @keyword
