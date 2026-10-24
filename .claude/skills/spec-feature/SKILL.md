---
name: spec-feature
description: Turn a new feature request into a written spec for Exp-LORer — pins down what the feature is (behavior, scope, explicit non-goals) using only docs/Specification.md, docs/Architecture.md, and docs/features/*.md. No source-code reading or graphify queries at this stage. Use when asked to plan, spec, or scope a new feature, before any implementation plan exists.
---

# Spec a Feature

First stage of this repo's spec → plan → implement cycle (see `CLAUDE.md`'s Development
Workflow). Defines *what* the feature is, not *how* it's built.

## When to Use

- A feature request comes in and no `.claude/active_work/spec.md` exists yet, or the user
  explicitly asks to (re)spec a feature
- Not for bug fixes — those are done directly, no spec/plan cycle
- Not for turning an existing, reviewed spec into an implementation plan — that's `plan-feature`

## Steps

1. Read only `docs/Specification.md`, `docs/Architecture.md`, and `docs/features/*.md`. Do not
   read source code and do not query graphify at this stage — scope is docs-only.
2. Use plan mode as normal. Ask the user whatever questions are needed to pin down a clear
   definition, behavior, scope, and explicit non-goals.
3. Write the result to `.claude/active_work/spec.md` (create `.claude/active_work/` if it doesn't
   exist), overwriting any existing `spec.md`.
4. If the feature changes a requirement described in `Specification.md`, update it in the same
   turn — don't wait to be asked.
5. Stop there. Do not produce an implementation plan or touch `plan.md` in this stage.

## After

The user reviews `spec.md` and approves it or edits it directly. No further action until they ask
for a plan — then hand off to `plan-feature`.
