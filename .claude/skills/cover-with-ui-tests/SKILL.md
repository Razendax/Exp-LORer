---
name: cover-with-ui-tests
description: Orchestrates adding ui_tests/ coverage for one feature end to end -- spawns a ui-test-coverage-writer agent to write and run new tests, then a ui-test-bugfix agent per unresolved failure to fix real bugs it finds. Use when the user invokes /cover-with-ui-tests <feature>, or asks to "cover <feature> with UI tests" / "add ui_tests coverage for <feature>" as an end-to-end request (test writing through bug fixing), not for writing a single test file by hand.
---

# /cover-with-ui-tests

You are the orchestrator only. **Do not write, edit, or run anything yourself in this flow** —
no test files, no source files, no pytest invocations, no builds. Your only job is to spawn the
right agents in the right order, relay information between them, and summarize for the user. If
you catch yourself about to Read/Write/Edit/Bash to make progress directly, stop — that work
belongs to one of the two agents below.

The feature description is whatever text follows `/cover-with-ui-tests`. If it's empty or too
vague to act on, ask the user what feature to cover before spawning anything.

## Step 1 — spawn the writer

Spawn one agent with `subagent_type: "ui-test-coverage-writer"` (fresh agent, **not** a fork — you
need this to block and return a result before you decide what to do next, not run in the
background). Pass it the feature description verbatim plus enough framing that it knows this is
for the Exp-LORer `ui_tests/` suite. Do not pre-research the feature yourself first — the writer
agent does its own exploration of the docs/code; handing it your own guesses only biases it.

Wait for its report (format defined in its own agent file — a "Bugs found" section listing
unresolved failures, or none if everything passed/was self-fixed).

## Step 2 — group suspected-shared-cause bugs, then handle one group at a time

If the writer's report has no "Bugs found" section, skip to Step 3.

Otherwise, first group the reported bugs by *suspected* shared root cause — a light judgment call,
not a rigorous analysis (the bugfix agent itself will verify or refute it). Group bugs together
when the writer's "suspected area" names the same file/component, or the expected-vs-actual
descriptions clearly point at the same mechanism (e.g. several tests all failing to persist the
same setting, or all showing the same stale-UI-after-action symptom). Leave a bug in its own
group of one whenever you're not reasonably confident it shares a cause with another — a wrong
split just costs one extra agent call, but a wrong merge wastes a whole investigation.

Then spawn one `ui-test-bugfix` agent **per group**, passing it every test in that group (id,
expected-vs-actual, suspected area, verbatim from the writer's report). Wait for each group's agent
to finish **before spawning the next group's**. Do not batch groups into parallel Agent calls and
do not fork them.

This sequencing is required, not just conservative: every bugfix agent builds into the same shared
`build/Release` directory via `build.ps1`, which also kills the running `Exp-LORer.exe` first.
Running two of these concurrently — whether two groups, or your group-level call racing one the
bugfix agent spawns internally — would corrupt that build. You only need to guarantee one thing:
never have two of your own top-level `ui-test-bugfix` calls in flight at once. Each agent already
handles its own internal splitting (spawning a sibling for a subset that turns out to have a
different root cause) sequentially on its own, so you don't need to manage that — just wait for the
one call you made for that group to fully return, including any recursive delegation it did.

If a bugfix agent's final report shows it (or something it delegated to) could not fix an issue
(build kept failing, or it concluded a test's own expectation was wrong), do not retry it yourself
and do not spawn another agent for it — just carry that outcome into the final summary for the
user to decide on.

## Step 3 — summarize for the user

Produce one summary covering:
- What test file(s)/methods were added, and the feature they cover.
- Final pass/fail state of every test the writer created.
- For every bug that went through Step 2: root cause, files changed, and whether verification
  passed.
- Anything a bugfix agent couldn't resolve, flagged clearly so the user knows it still needs
  attention.

Do not re-run anything yourself to double check — relay what the agents reported.
