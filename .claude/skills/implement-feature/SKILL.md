---
name: implement-feature
description: Implement an Exp-LORer feature from .claude/active_work/plan.md, in whole or by specific milestone(s), by orchestrating one feature-implementer agent per milestone, marking plan.md and committing after each. Use when asked to implement, build, or code a feature that already has a plan.md.
---

# Implement a Feature

Third stage of this repo's spec → plan → implement cycle (see `CLAUDE.md`'s Development
Workflow). You are the orchestrator — you read `plan.md` to know the milestones, delegate each
one to its own `feature-implementer` agent, and handle the bookkeeping (`plan.md`, git commit)
around each agent's result. **Never write or edit source code yourself, and never run a build or
test command yourself** — that's the agent's job, one call per milestone. Reading/editing
`plan.md` and committing are yours.

## When to Use

- `.claude/active_work/plan.md` exists and the user asks to implement it, in whole or by named
  milestone(s)
- Not before a plan exists — run `spec-feature` then `plan-feature` first
- Not for bug fixes — those are done directly, no spec/plan cycle

## Steps

1. Read `.claude/active_work/plan.md` to get the ordered milestone list and which are already
   marked done.
2. Determine scope from the user's request:
   - No milestone named → every not-yet-done milestone, in plan order.
   - One or more milestones named → exactly those, in the order they appear in the plan
     (regardless of the order the user listed them), even if some are already marked done — if a
     named one is already done, confirm with the user whether to redo it rather than silently
     skipping or silently re-running it.
3. For each milestone in scope, **one at a time, waiting for each to fully finish before starting
   the next**:
   a. Spawn one `feature-implementer` agent (fresh, not a fork), passing it that milestone's full
      text from `plan.md` verbatim. Milestones can depend on earlier ones, so also tell it which
      milestone(s) are already done (per `plan.md`'s own marks, plus any completed earlier in this
      same loop) and pass along whatever concrete, already-known details would help it build on
      that prior work — e.g. file paths/symbols from an earlier milestone's own report in this
      run, or specifics already sitting in `plan.md`/your context. Don't go research this
      specially (no extra graphify queries, file reads, or exploration just to brief it) — only
      relay what you already have in hand. Never run two `feature-implementer` agents
      concurrently — they share `build/Release`.
   b. If its report shows `Status: blocked` — stop the loop entirely. Do not mark `plan.md`, do
      not commit, do not continue to the next milestone. Carry the blocker into your final summary
      for the user to resolve.
   c. If its report shows `Status: done` — edit `.claude/active_work/plan.md` yourself, marking
      this milestone done (matching however the file already tracks progress). If every milestone
      in the plan is now done, rename `plan.md` to `prev_plan.md`, overwriting any existing one.
   d. Invoke the `git-commit` skill to commit this milestone's implementation diff as one commit,
      following its staging checklist and message format exactly — one commit per milestone, not
      a bundle of several. `.claude/active_work/` (including `plan.md`/`prev_plan.md`) is
      gitignored — it never gets staged and the step c edit is not part of this commit; only the
      source/doc changes the agent made go in.
4. After the loop ends (whether by finishing all of scope or stopping on a blocker), summarize for
   the user: which milestone(s) were completed and committed, build/test result for each (relayed
   from the agents, not re-verified by you), and the resulting state of `plan.md`.

## Also Applies Here

`CLAUDE.md`'s "do not smoke test" rule and its UI blackbox test note apply fully to this stage
(and to direct bug fixes) — the `feature-implementer` agent follows them, not you.
