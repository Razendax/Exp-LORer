---
name: coding-standards
description: Language-agnostic coding standards checklist (naming, function size, error handling, comments, testing, commit hygiene). Use when explicitly reviewing code quality/conventions, writing a style guide, setting up linting rules, or onboarding contributors to a project's conventions — not as a general reminder on every coding task.
---

# Coding Standards & Best Practices

Language-agnostic conventions for code that isn't covered by a language-specific
guideline. For C++, defer to [[cpp-core-guideline]] instead — this skill fills
the gaps for everything else and for cross-language review.

## When to Use

- Writing a project's CONTRIBUTING/style-guide doc
- Reviewing a PR specifically for quality/convention issues (naming, structure, size)
- Setting up linter/formatter/type-checker configuration
- Onboarding a contributor to a project's conventions
- Arbitrating a naming/structure disagreement ("should this be one function or three?")

## When NOT to Use

- Routine feature work or bug fixes — good practice here should come from default
  judgment, not a checklist invoked on every turn
- A language with its own guideline skill available (e.g. C++) — use that instead
- Pure logic/algorithm questions with no style dimension

## Cross-Cutting Principles

- **Readability over cleverness**: code is read far more often than written; optimize for the next reader, not for fewest keystrokes
- **KISS**: ship the simplest solution that satisfies today's requirement; no speculative flexibility
- **DRY, but not prematurely**: extract shared logic once a duplication actually causes a maintenance problem — two occurrences can stay duplicated, three is a signal
- **YAGNI**: don't add parameters, hooks, or abstraction layers for a use case that doesn't exist yet

## Naming

| Rule | Example |
| --- | --- |
| Name for what it *is*, not how it's computed | `activeUsers`, not `filteredList2` |
| Booleans read as predicates | `isValid`, `hasPermission`, not `valid`, `flag` |
| Avoid noise words | `data`, `info`, `manager`, `helper` say nothing — name the actual responsibility |
| Match the codebase's existing casing/verbosity convention | don't mix `snake_case` and `camelCase` in the same file |
| Units and scale go in the name | `timeoutMs`, `sizeBytes`, not bare `timeout`, `size` |

## Functions & Structure

- One function does one job describable without "and"; if the name needs "and", split it
- Keep argument count low (roughly ≤4); bundle related parameters into a struct/object instead of adding more positional args
- Prefer early returns / guard clauses over deep nesting
- A function's abstraction level should be consistent — don't mix high-level orchestration with low-level string parsing in the same body

## Comments & Documentation

- Default to no comment; self-documenting names and structure come first
- Write a comment only for the *why*: a non-obvious constraint, a workaround for a specific bug, a subtlety that would surprise the next reader
- Keep comments concise and to the point — one short line beats a paragraph; if it needs more than a sentence, the code likely needs a clearer name or a doc comment instead
- Never write a comment that restates the code, references a ticket/PR, or narrates "removed X" — these rot as the code evolves
- Exception to "no *what* comments": when a line calls into an OS/platform API or an unfamiliar third-party function/class whose behavior isn't obvious from its name (e.g. a Win32 call, an obscure stdlib function), add a very short comment naming what it does — this documents the API for a reader unfamiliar with it, not the surrounding logic
- Public APIs get a short doc comment describing the contract (inputs, outputs, side effects, error conditions) — not an implementation narration

## Error Handling

- Fail at the boundary: validate untrusted input (user input, external APIs, file/network data) at the edge; trust internal calls and framework guarantees beyond that
- Don't swallow errors silently — propagate, log with context, or handle explicitly; an empty catch block is almost always a bug
- Prefer typed/explicit error signaling (exceptions, `Result`/`Option`, error returns — whatever the language's idiom is) over sentinel values like `-1` or `null` that callers can forget to check
- Don't add error handling for scenarios that structurally cannot happen

## Testing

- A behavior change needs a test that would fail without it — otherwise the change isn't verified, it's asserted
- Test names describe the scenario and expected outcome, not the implementation
- Prefer a few tests at the right boundary (public behavior) over many brittle tests pinned to internals

## Version Control Hygiene

- Commits are scoped to one logical change; don't bundle an unrelated cleanup into a bug-fix commit
- Commit messages explain *why*, not a restatement of the diff
- Don't leave debug prints, commented-out code, or TODOs without an owner/issue reference in the merged result

## Red Flags Checklist (for review)

- [ ] Function/file doing more than one job
- [ ] Names that require a comment to explain
- [ ] Copy-pasted block appearing 3+ times
- [ ] Speculative parameter/flag with no current caller
- [ ] Silent `catch`/ignored error return
- [ ] Comment describing *what* instead of *why* (unless it's the OS/unfamiliar-API exception above)
- [ ] Verbose/multi-sentence comment where a short one would do
- [ ] OS/platform or unfamiliar library call with no short note on what it does
- [ ] Inconsistent naming convention within the same file/module
