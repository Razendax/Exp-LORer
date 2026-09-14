---
name: code-audit
description: Full-spectrum code review covering bugs/logic errors, security vulnerabilities, performance issues, and style violations in one pass. Groups every finding by severity (Critical/High/Medium/Low/Info) and pairs each with a suggested fix, shown as a before/after code example. Use when the user asks for a thorough/full review, a bug-and-security-and-style audit, or wants findings ranked by severity — not for a routine correctness-only diff review (the built-in `code-review` skill) or a security-only pass (the built-in `security-review` skill), and not as a reminder on every edit.
---

# Code Audit

A single review pass across four lenses — bugs/logic, security, performance, style —
producing one severity-ranked report with fixes. This is broader and slower than the
built-in `code-review` skill (correctness + simplification on a diff) or `security-review`
(security only); reach for those when the user only wants one lens. For C++ specifics
(RAII, ownership, concurrency rules), this skill applies [[cpp-core-guideline]] rather
than restating it; for naming/comment/testing conventions it applies [[coding-standards]],
[[testing-standards]], and [[doc-comments]] the same way.

## When to Use

- User asks for a "full", "thorough", or "comprehensive" code review
- User asks to check for bugs *and* security issues *and* style in the same pass
- User wants findings organized/ranked by severity with concrete fixes, not just a list
- Pre-merge audit of a non-trivial PR or a batch of new files

## When NOT to Use

- A quick correctness check on the current diff — use the built-in `code-review` skill
- A security-only pass — use the built-in `security-review` skill
- Routine small edits — don't invoke this on every change; it's a deliberate, heavier pass
- Pure style/convention question with no bug or security dimension — use [[coding-standards]]
  or [[cpp-core-guideline]] directly

## Process

1. **Scope it.** Default to `git diff` against `main` (or the target branch) when nothing
   is specified; honor an explicit PR/branch/file-list if the user gave one. Read each
   touched file **in full**, not just the diff hunks — several checks below (layering,
   threading, ownership) only make sense with surrounding context.
2. **Check architecture fit first, for files under `src/`.** Cross-reference
   `Architecture.md`/`Specification.md` and `CLAUDE.md`: does `src/domain` or
   `src/application` pull in Qt/SQLite/FFmpeg headers directly? Does a Port
   (`IFileSystemRepository`, `ITagRepository`, `IMediaDecoder`) get bypassed? Is disk/DB
   I/O happening on the Qt UI thread? Is a path carried as `std::string` instead of
   `std::filesystem::path`, or an error signaled by exception/sentinel instead of
   `std::expected<T, Error>`? These are architecture violations, not style — file them as
   Medium/High per the severity table below, not as nits.
3. **Apply the four lenses** (below) to every touched file.
4. **Merge and dedupe** — one finding per root cause, not one per line it appears on.
5. **Emit the report** using the format in Output, grouped by severity, omitting empty
   sections. If nothing is found in a category, don't pad the report — say so briefly and
   move on. If the audit is clean, say that plainly instead of inventing nits.

## Lens 1 — Bugs & Logic Errors

- Off-by-one / boundary conditions; inverted or short-circuited boolean logic
- `std::expected` misuse: errors constructed but never returned, error variants mapped to
  the wrong case, or a caller unwrapping without checking `has_value()`
- Resource/RAII violations, use-after-move, dangling references, iterator invalidation —
  see [[cpp-core-guideline]] Resource Management and Expressions sections for the
  canonical patterns
- Race conditions and shared mutable state crossing a thread boundary without
  synchronization — see [[cpp-core-guideline]] Concurrency section
- Silent failure: caught exception or error result that's logged (or dropped) but the
  caller proceeds as if it succeeded

## Lens 2 — Security Vulnerabilities

Adapted for a Windows-first desktop file explorer that parses untrusted file/media
content and shells out to FFmpeg — the attacker-controlled inputs are file paths, file
*contents*, and media metadata, not network requests.

- **Path handling**: unsanitized `..`/absolute-path segments crossing a trust boundary;
  incorrect long-path (`\\?\`) handling in `StandardFileSystemRepository` that silently
  truncates or misresolves a path; symlink/junction TOCTOU between a check and the
  subsequent file operation
- **SQL injection**: any string-concatenated SQL in `SQLiteTagRepository` — must be
  parameterized/bound, no exceptions
- **Process/command injection**: file paths or names passed to FFmpeg (or any subprocess)
  via a shell instead of an argv array; unescaped filenames reaching a command line
- **Memory safety on untrusted input**: media/thumbnail decoding is the classic attack
  surface — unchecked sizes/lengths from file headers, unsafe casts, buffer arithmetic
  on decoder output; flag any raw pointer arithmetic touching file-derived data
- **IPC hardening**: the single-instance named-mutex/local-IPC path forwards startup args
  to a running instance — those args must be validated the same as any other untrusted
  input, not executed/opened blindly
- **DLL/library loading (Windows)**: search-order hijacking risk if a plugin/library is
  loaded by bare filename instead of a fully qualified, trusted path
- **Logging**: full user file paths or filenames logged verbatim can leak more than
  intended into shared logs — flag if it looks incidental rather than deliberate

## Lens 3 — Performance Issues

- Disk/DB/media I/O invoked on the Qt UI thread — a hard violation of the threading model
  in `Architecture.md`/`CLAUDE.md`, not just a perf nit; rate it High
- N+1 query patterns in `SQLiteTagRepository` (looping single-row queries instead of one
  batched query)
- Unnecessary copies of `std::filesystem::path`, `std::string`, or large value types where
  `const&` or move would do — see [[cpp-core-guideline]] F.16
- Re-hashing a file's xxHash64 partial hash when a cached value keyed by path+mtime is
  still valid
- Thumbnail/media decoding blocking the UI or lacking cancellation for items scrolled past
- Quadratic scans over large directory listings; missing an obvious index for a hot query

## Lens 4 — Style Violations

Delegate to the existing skills instead of restating their checklists:
- General naming/structure/comments/tests: [[coding-standards]]
- C++-specific idiom (RAII, `const`, casts, enums, templates): [[cpp-core-guideline]]
- Public API doc comments: [[doc-comments]]
- Test structure/coverage: [[testing-standards]]

Project-specific conventions to check directly (from `CLAUDE.md`): `PascalCase` types,
`camelCase` methods/variables, `m_` prefix on private members, `I` prefix on Port
interfaces, UTF-8 conversion happening only at the SQLite boundary.

## Severity Levels

| Severity | Criteria |
| --- | --- |
| **Critical** | Exploitable by untrusted input (crafted filename/media file), memory-safety bug, data loss/corruption, crash on a common path |
| **High** | Correctness bug breaking a core feature, or a hard architecture-rule violation (Qt/SQLite leaking into domain/application, blocking I/O on the UI thread, SQL built by string concatenation) |
| **Medium** | Real but bounded-impact performance issue, logic error confined to an edge case, missing error handling at a boundary |
| **Low** | Style/naming/comment convention violation, minor inefficiency, missing test for new behavior |
| **Info** | Suggestion with no functional impact — worth mentioning, not worth blocking on |

## Output Format

One Markdown report, sections in this order, each finding under its severity heading and
sorted by file path within the section. Omit a heading entirely if it has no findings.

```
## Critical
## High
## Medium
## Low
## Info
```

Each finding:

```
### [SEVERITY] Short title — path/to/file.cpp:123
**Issue:** what's wrong, stated plainly.
**Why it matters:** the concrete failure this causes (crash/exploit/perf hit/etc.), not a
restatement of the issue.
**Suggested fix:**
​```cpp
// before
...
// after
...
​```
```

Close with a one-line total (e.g. "3 Critical, 1 High, 4 Low — 0 Medium/Info").

## Red Flags Checklist (quick scan while reading)

- [ ] Qt/SQLite/FFmpeg header included under `src/domain` or `src/application`
- [ ] Disk/DB/media call reachable from the UI thread without dispatching to a Port
- [ ] SQL string built by concatenation instead of bound parameters
- [ ] Subprocess (FFmpeg) invoked via a shell string instead of an argv array
- [ ] Path stored/passed as `std::string` instead of `std::filesystem::path`
- [ ] Error swallowed: caught/logged but caller proceeds unchecked
- [ ] Raw pointer arithmetic over file-derived/decoder-output bytes
- [ ] Named mutex/IPC-forwarded args used before validation
- [ ] New public function with no doc comment ([[doc-comments]]) or no test ([[testing-standards]])
