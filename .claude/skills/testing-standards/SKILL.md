---
name: testing-standards
description: Language-agnostic testing standards checklist (structure, naming, coverage philosophy, mocking, flakiness). Use when writing new tests, reviewing a PR specifically for test quality, setting up a test suite/framework, or deciding what and how much to test — not as a reminder invoked on every code change.
---

# Testing Standards & Best Practices

Language-agnostic conventions for writing and reviewing automated tests. Pairs
with [[coding-standards]] (production code conventions) and, for C++
specifics — fixture/mock idioms, RAII in tests — [[cpp-core-guideline]].

## When to Use

- Writing tests for new or changed behavior
- Reviewing a PR specifically for test quality/coverage (not just the production code)
- Setting up a test suite, framework, or CI test-run configuration
- Deciding whether a change needs a new test, an updated test, or no test at all
- Arbitrating a testing disagreement ("should this be mocked?", "is this test worth keeping?")

## When NOT to Use

- Routine feature work where test-writing is incidental — good judgment applies without
  invoking a checklist every time
- Debugging a specific failing test's logic (that's a debugging task, not a standards question)
- Pure production-code review with no test dimension — use [[coding-standards]] instead

## Cross-Cutting Principles

- **A test is a specification**: it should read as a statement of behavior, not an
  implementation trace — a reader who has never seen the code should understand
  *what* is guaranteed from the test alone
- **Test behavior, not implementation**: assert on inputs/outputs and observable
  side effects; a refactor that preserves behavior should never break a test
- **Every test earns its keep**: a test that never fails when the code is wrong is
  worse than no test — it's false confidence
- **Fast and deterministic beats thorough and flaky**: a flaky or slow test gets
  ignored or deleted under pressure, which defeats its purpose

## What Needs a Test

- Any behavior change needs a test that would **fail without the change** —
  otherwise the change is asserted, not verified
- Bug fixes get a regression test reproducing the original failure first, then the fix
- New public/exported functions get tests at their boundary (contract), not for
  every internal helper they happen to call
- Skip tests for: pure pass-through code, generated code, trivial
  getters/setters with no logic, and framework-guaranteed behavior

## Structure & Naming

| Rule | Example |
| --- | --- |
| Arrange-Act-Assert (or Given-When-Then), one block each, visually separated by an empty line | setup, single action under test, assertions |
| Test name states scenario + expected outcome, not method name | `RejectsWithdrawal_WhenBalanceInsufficient`, not `TestWithdraw2` |
| One logical assertion focus per test | multiple `assert`/`EXPECT` calls are fine if they check one behavior's facets; don't test two unrelated behaviors in one test |
| No conditional logic (`if`/loops) inside a test | a test with branches is testing itself; write two tests instead |
| Shared setup goes in a fixture/helper, not copy-pasted per test | keeps each test's arrange step short and the divergence between tests visible |

## Test Doubles (Mocks, Stubs, Fakes)

- Mock at architectural boundaries (network, filesystem, external services, clock,
  randomness) — not internal collaborators within the same module
- Prefer a fake (real logic, simplified) over a mock (recorded expectations) when
  the real behavior is simple enough to reimplement faithfully — fakes catch more
  real bugs
- Never mock the thing under test, and never mock so much that the test only
  verifies its own mock setup
- A mock-heavy test that still passes after the production code is deleted is a
  sign the test verifies wiring, not behavior — see [[coding-standards]]'s
  "false confidence" concern under Testing

## Test Types & Scope

- **Unit**: one function/class, all collaborators faked or excluded; fast (ms), the bulk of the suite
- **Integration**: real collaborators across a boundary (e.g., real DB, real filesystem); fewer, slower, catches wiring bugs unit tests can't
- **End-to-end**: full system through its real interface; fewest, reserved for critical user-facing paths
- Push coverage down the pyramid: prefer a unit test over an integration test over
  an e2e test whenever the unit test can express the same guarantee — cheaper to
  run and to debug when it fails

## Flakiness & Determinism

- No reliance on wall-clock time, real network calls, unseeded randomness, or test
  execution order — inject a controllable clock/seed instead
- A flaky test gets fixed or deleted immediately, not silently retried or skipped —
  a suite people don't trust stops getting run
- Tests must be independent: any test can run alone or in any order and get the
  same result; shared mutable state between tests is a bug

## Coverage Philosophy

- Coverage percentage is a signal for *untested* code, not a target to hit —
  100% coverage with weak assertions is worse than 80% with strong ones
- Favor a few tests at the public-behavior boundary over many brittle tests
  pinned to internals (mirrors [[coding-standards]])
- New code in a changed file should be covered; retrofitting full coverage onto
  untouched legacy code is a separate, deliberate task — not a side effect of an
  unrelated PR

## Red Flags Checklist (for review)

- [ ] Test name doesn't describe the scenario/expected outcome
- [ ] Assertion-free test (only checks "it didn't throw")
- [ ] `if`/loop/branch inside a test body
- [ ] Mock of the class/function actually under test
- [ ] Test depends on execution order or another test's leftover state
- [ ] Sleep-based timing instead of an injected/controllable clock
- [ ] Skipped/disabled test with no tracked reason
- [ ] Test that duplicates another test's exact scenario
