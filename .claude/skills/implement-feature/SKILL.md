---
name: implement-feature
description: Implement an Exp-LORer feature from .claude/active_work/plan.md, in whole or by a specific milestone, querying the graphify-out/ knowledge graph for exact call sites and affected callers before editing. Marks completed milestones done in plan.md, renaming it to prev_plan.md only once every milestone is finished. Use when asked to implement, build, or code a feature that already has a plan.md.
---

# Implement a Feature

Third stage of this repo's spec → plan → implement cycle (see `CLAUDE.md`'s Development
Workflow). Carries out the *how* from `plan.md`.

## When to Use

- `.claude/active_work/plan.md` exists and the user asks to implement it, in whole or by a named
  milestone
- Not before a plan exists — run `spec-feature` then `plan-feature` first
- Not for bug fixes — those are done directly, no spec/plan cycle

## Steps

1. Query graphify (see the `graphify` skill) as needed while working through the plan to locate
   exact call sites, affected callers, and related code before editing.
2. Follow the plan (or just the requested milestone).
3. Only once every milestone in the plan has been implemented, rename
   `.claude/active_work/plan.md` to `.claude/active_work/prev_plan.md`, overwriting any existing
   one. If only some milestones are done, leave `plan.md` in place and instead mark the completed
   milestone(s) done within the file, so the remaining milestones aren't lost.

## Also Applies Here

`CLAUDE.md`'s "do not smoke test" rule and its UI blackbox test note apply fully to this stage
(and to direct bug fixes) — see `CLAUDE.md`'s Development Workflow section.
