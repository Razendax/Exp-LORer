---
name: doc-comments
description: Language-agnostic standards for API/public doc comments (Doxygen, JSDoc, docstrings, XML doc, etc.) — what to document, what to skip, and how to keep docs from rotting. Use when writing or reviewing doc comments on public APIs, setting up a doc-generation pipeline, or deciding whether a function needs a doc comment at all — not as a reminder on every function written.
---

# Doc-Comment Standards

Language-agnostic conventions for API documentation comments — the
`/** ... */`, `///`, `"""..."""`, JSDoc, or XML-doc block attached to a
declaration, as distinct from inline `//` comments explaining *why* a line of
code does something (covered by [[coding-standards]]'s Comments section). For
C++ Doxygen specifics (tag set, `\brief`/`\param`/`\return` conventions),
combine with [[cpp-core-guideline]].

## When to Use

- Writing a doc comment for a new public function, class, or module
- Reviewing a PR specifically for documentation quality/completeness
- Setting up a doc-generation pipeline (Doxygen, JSDoc, Sphinx, rustdoc, etc.)
- Deciding whether something needs a doc comment at all
- Auditing a codebase for stale/misleading documentation

## When NOT to Use

- Inline comments explaining a non-obvious line of logic — that's
  [[coding-standards]]'s territory, not this skill's
- Private/internal helpers with no external consumer — see "What Needs a
  Doc Comment" below
- Routine feature work where documenting is incidental — write good docs by
  default judgment, don't invoke a checklist per function

## Cross-Cutting Principles

- **Document the contract, not the implementation**: a doc comment tells a
  caller what they can rely on without reading the body — inputs, outputs,
  side effects, failure modes. It is not a narration of the code.
- **A stale doc is worse than no doc**: a comment that no longer matches the
  code actively misleads; an absent one just leaves the reader to check the
  source
- **Document the surprising, not the obvious**: if the signature already says
  it, the doc comment restating it adds noise, not information
- **Doc comments are for consumers, not authors**: write for someone calling
  this API from outside, who cannot see (and shouldn't need to see) the body

## What Needs a Doc Comment

| Needs one | Usually doesn't |
| --- | --- |
| Public/exported functions, classes, modules | Private/internal helpers with a self-explanatory name and one call site |
| Anything with non-obvious preconditions, side effects, or failure modes | Trivial getters/setters (`getName()` returning `name`) |
| Public API entry points consumed outside the file/module | Code obvious from name + type signature alone |
| Interfaces/abstract base classes defining a contract implementers must honor | Test code (tests document themselves via naming — see [[testing-standards]]) |

If a function is public but genuinely self-explanatory from its signature, a
one-line doc comment stating the contract still beats none — the ambiguity is
whether it needs *more* than one line, not whether it needs one.

## What Goes In It (the Contract)

- **Purpose**: one line, what it does and why it exists — not how
- **Parameters**: meaning and constraints (valid ranges, nullability,
  ownership) for anything not obvious from the name/type
- **Return value**: what's returned, including edge cases (empty
  collection vs. null/None, sentinel values)
- **Errors/exceptions**: what can go wrong and how it's signaled — thrown
  exception types, error codes, `Result`/`Option` variants
- **Preconditions/postconditions**: state the caller must establish before
  calling, and state guaranteed after
- **Side effects**: mutation of shared state, I/O, logging — anything beyond
  computing and returning a value
- **Thread-safety**: only when it's not the module's blanket default —
  don't repeat it on every function if the whole class documents it once
- Skip a section entirely rather than writing "N/A" or restating the
  parameter name — an omitted section is quieter than a filled-in non-answer

## Format & Placement

- Use the ecosystem's standard tool/tag set (Doxygen for C++, JSDoc for
  JS/TS, docstrings for Python, XML doc for C#, rustdoc for Rust) — don't
  invent a bespoke format
- One doc comment per declaration, directly above it, no blank line between
  comment and signature
- Keep the first line a complete, standalone summary sentence — many tools
  (IDE hovers, generated index pages) show only that line
- Code examples in docs must be runnable/compilable as written — an example
  that doesn't compile is worse than no example
- Match the file/module's existing tag style and verbosity; don't mix
  conventions within one codebase

## Keeping Docs From Rotting

- A doc comment describing behavior changed by a PR gets updated in the
  *same* PR — never as a follow-up
- Prefer documenting invariants that are true by construction (interface
  contracts) over incidental details of the current implementation, which
  drift fastest
- If a linter/doc-generator can enforce presence (e.g., "public API missing
  doc comment"), rely on that instead of catching it by eye in review
- When in doubt whether a doc comment is still accurate, trust the code and
  fix the comment — never the reverse

## Red Flags Checklist (for review)

- [ ] Doc comment restates the function name/signature with no new information
- [ ] Comment describes an old parameter/return type that no longer matches the signature
- [ ] Missing doc comment on a public API entry point
- [ ] Doc comment on a private, single-call-site helper (noise, not documentation)
- [ ] Undocumented thrown exception / error path a caller must handle
- [ ] "TODO: document this" or an empty doc block left in place
- [ ] Code example in a doc comment that wouldn't actually compile/run
