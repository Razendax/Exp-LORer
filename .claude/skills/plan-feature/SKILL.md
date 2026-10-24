---
name: plan-feature
description: Turn a reviewed .claude/active_work/spec.md into an implementation plan for Exp-LORer, divided into vertical-slice milestones — each one wires a complete, user-testable path through every layer it touches, never backend/domain work in one milestone with its UI hookup left for a later one. Queries the graphify-out/ knowledge graph for affected files/symbols first. Use when asked to turn a spec into a plan, or to plan a feature that already has a spec.md.
---

# Plan a Feature

Second stage of this repo's spec → plan → implement cycle (see `CLAUDE.md`'s Development
Workflow). Turns the *what* from `spec.md` into *how*.

## When to Use

- `.claude/active_work/spec.md` exists and the user asks to turn it into a plan
- Not before a spec exists — run `spec-feature` first
- Not for carrying out the plan itself — that's `implement-feature`

## Steps

1. Read `.claude/active_work/spec.md` fresh — don't rely on conversation memory, the user may
   have edited it.
2. Query graphify (see the `graphify` skill) for the areas/files/symbols the feature will touch
   before producing the plan — it's faster and more complete than grepping cold, and supplements
   but never replaces `docs/Architecture.md`/`docs/Specification.md` as the source of truth for
   intended design.
3. Produce the implementation plan (use plan mode as normal). If it's a big feature, divide it
   into milestones with enough detail in each to implement it standalone.

   **Milestones must be vertical slices, not horizontal layers.** Each milestone delivers a
   complete, user-testable path through every layer it touches (domain → application → adapters
   → UI) for some subset of the feature's behavior, even if that subset is small or incomplete
   relative to the full feature. Never put backend/domain/adapter work in one milestone and its UI
   hookup in a later one — a milestone that adds a Port method, repository call, or use case but
   leaves it unwired from the UI is not done; wire it end-to-end (even behind a minimal/placeholder
   UI) in the same milestone so the user can exercise it. Split by *behavior* (e.g. "show X",
   "filter by Y", "edit Z"), not by *layer*.
4. Save it to `.claude/active_work/plan.md`, overwriting any existing one.
5. If the feature changes the layering, Ports, schema, or other design described in
   `Architecture.md`, update it in the same turn — don't wait to be asked. This includes appending
   a new §14.N decision-log entry to whichever `docs/features/*.md` file its topic fits best (see
   `CLAUDE.md`'s project-overview note on picking the file and updating its "Covers §..." intro).
6. Rename `.claude/active_work/spec.md` to `.claude/active_work/prev_spec.md`, overwriting any
   existing one.
7. Stop there. The user reviews/edits the plan and doc changes before implementation starts.

## After

The user reviews `plan.md` (and any doc changes) and edits as needed. No further action until
they ask to implement — then hand off to `implement-feature`.
