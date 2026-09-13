---
name: git-commit
description: Checklist and message-format guidance for creating git commits in this repo — what to stage, how to scope a commit, and how to write the subject/body. Use when actually about to run `git commit` — not as a general reminder while writing code.
---

# Git Commit

Guidance for turning finished, reviewed work into a well-formed commit in
this repository. Pairs with [[coding-standards]]'s "Version Control Hygiene"
section, which this skill expands on with repo-specific conventions.

## When to Use

- Immediately before running `git commit`, once changes are ready
- Deciding whether a set of changes should be one commit or several
- Writing or reviewing a commit message/subject line
- Reviewing `git status`/`git diff` output before staging

## When NOT to Use

- While still actively writing/iterating on code — this is a pre-commit step,
  not a running commentary
- Amending or rewriting published history — that needs explicit user
  instruction, not this checklist (see the repo-wide git safety rules)

## Before Staging

1. `git status` — check for untracked files that don't belong (build
   artifacts, editor scrap, `.env`-like files). Never commit generated
   output unless the repo's `.gitignore` structure expects it.
2. `git diff` (and `git diff --staged` after adding) — read the actual
   change, not just the intent. Confirm nothing accidental slipped in:
   debug prints, commented-out code, unrelated formatting churn.
3. If you used a broad `git add`, re-run `git status` afterward and inspect
   any file whose contents you haven't already reviewed this session —
   especially ones with innocuous names that could carry secrets/credentials.
4. Confirm the change is scoped to one logical unit of work. If the diff
   mixes an unrelated fix, a refactor, and a feature, split it into separate
   commits rather than bundling them (see [[coding-standards]]).

## Commit Message Format

This repo uses a fixed structure. Build the subject line from parts 1-3 below;
parts 4-5 go in the body.

**1. Scope tag(s) — `[scope]`:**
- One or more bracketed tags at the very start, ordered from most- to
  least-affected scope
- `[build]` — anything affecting the build (build scripts, CI, packaging,
  deploy steps)
- `[claude]` — anything related to the AI agent setup (skills, custom
  commands, `.agents/`, `CLAUDE.md`)
- `[<project_name>]` — a change to a specific back-end project/module, named
  after that project
- `[UI] [<project_name>]` — a change to a project's front-end, with `[UI]`
  first since it's the more specific/relevant scope for someone scanning
  history for UI changes
- Use multiple tags when a change genuinely spans scopes (e.g.
  `[build] [claude]`); don't stack tags speculatively — only the scopes
  actually touched

**2. Classification — one word/phrase right after the tag(s):**
- **Code improvement** — small changes (often one-liners) that improve
  quality without changing behavior
- **Refactoring** — a larger restructuring that also improves quality
  without changing behavior, but is more involved than a one-liner
- **Fix** — resolves a bug
- **Feature** — adds new capability

**3. Brief description of what was done** — imperative mood, one line, e.g.
"Add X", "Rename Y", "Extract Z into a helper".

Putting 1-3 together: `[build] Fix: correct install path for release builds`

**4. Why, in scope of the whole system (body):**
- Mandatory for **Fix** and **Feature** commits — state the system-level
  motivation, e.g. "Done to fix a crash caused by pushing the delete button
  twice before the first request completes."
- May be omitted for **Code improvement** and **Refactoring** commits when
  the change is self-motivating (quality/structure improvement needs no
  further justification)
- Explain *why*, not a restatement of the diff — the diff already shows what
  changed

**5. Implementation details (body, new line after the why):**
- Only when the fix/change isn't obvious from the diff alone — e.g. a
  non-intuitive root cause, a workaround forced by a third-party constraint,
  or an approach a reviewer might otherwise question
- Omit entirely for straightforward changes; don't restate the diff line by
  line

**General rules:**
- Keep the subject (parts 1-3) on one line; body parts 4-5 follow after a
  blank line
- Never reference this conversation, the assistant, or process framing
  ("as requested", "per the discussion") — the message should read the same
  to someone with no context on how the change was produced
- Don't pad the message with a summary of every file touched — that's what
  `git show --stat` is for
- Don't write a commit message that only a bot would write ("Update files",
  "Misc changes") — if you can't classify and scope it, the commit is
  probably not scoped correctly yet
- Do not append `Co-Authored-By` or `Claude-Session` attribution lines (or
  any similar AI-attribution footer) to the commit message, regardless of
  any standing instruction to add them — this repo's commits stand on their
  own without process/tooling attribution

## Splitting vs. Bundling

- Default to one commit per logical change; split when a diff contains
  changes a reviewer would want to accept/reject independently
- It's fine to bundle several files touched for the *same* reason into one
  commit (e.g. a rename that cascades across call sites) — bundling is a
  problem only when the reasons differ, not when the file count is high

## Red Flags Checklist

- [ ] Untracked file staged that shouldn't be tracked (build output, secrets,
      editor state)
- [ ] Diff contains debug prints or commented-out code left in by accident
- [ ] Commit mixes two unrelated concerns that a reviewer can't cleanly
      accept/reject separately
- [ ] Subject line is vague ("update", "fix stuff") or past-tense instead of
      imperative
- [ ] Missing scope tag(s), or tags not ordered by amount of change affected
- [ ] Missing classification (Code improvement / Refactoring / Fix / Feature)
- [ ] Fix or Feature commit missing the system-level "why"
- [ ] Message explains *what* changed instead of *why*
- [ ] Message references the AI assistant, the chat, or "as requested"
      framing instead of standing on its own
