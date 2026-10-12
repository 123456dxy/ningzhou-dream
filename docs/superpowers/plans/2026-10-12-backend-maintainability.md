# Backend Maintainability Implementation Plan

> **For agentic workers:** Use parallel subagents with exclusive file ownership; root integrates and verifies. User explicitly requests no commits this round.

**Goal:** Before 20:30 Asia/Shanghai, synchronize the current project, rename its branch, make backend identifiers and source responsibilities maintainable, and document the collaborator's changes without altering public contracts.

**Architecture:** Typed business identifiers and a centralized text/catalog layer retain legacy wire values. API view generation and persistence responsibilities move into focused modules; public headers retain compatibility.

**Tech Stack:** C++20, CMake, Ninja/GCC on Windows, Visual Studio CMake project, existing C++ and JS contract tests.

**Spec:** ../specs/2026-10-12-backend-maintainability-design.md

## Global Constraints

- Baseline fa43732; previous owner baseline 0c24e4b.
- Work in the user's current checkout and use codex/backend-desktop-ui.
- Do not commit, push refactoring changes, or merge main.
- Preserve HTTP routes, JSON keys, old saves, mobile V2, multiplayer account isolation and frozen-shadow semantics.
- Do not modify each other's files or tests/test_main.cpp and tests/harness.h without root coordination.

## Review Focus

- Unknown IDs must not silently resolve to a valid business category.
- Renaming display text must not change game semantics or saved identifiers.
- Save/load round trips and detached snapshots must preserve RNG, calendar, active combat and account isolation.
- API views remain read-only and keep the authoritative preview/display contract used by both frontends.
- All moved translation units must be included in CMake and existing audit tooling.

## Tasks

- [x] Root: confirm merge ancestry, rename branch, fast-forward latest main, preserve local ignored saves/builds.
- [x] Root: immutable baseline build/test, review latest collaborator changes and write comparison report.
- [x] Worker A: implement typed identifiers and text table with compatibility tests; migrate owned combat/progression logic away from display-string comparisons and clarify local variable names.
- [x] Worker B: extract API view serialization into focused interface files; preserve route handlers and wire schema, clarify serializer variable names and run relevant contract tests.
- [x] Worker C: split save serializers/validation/file storage into focused persistence files with unchanged public functions and compatible save fixtures.
- [x] Root: classify existing implementation files by domain, update CMake/audits, add Visual Studio/MSVC-compatible configuration and module ownership documentation.
- [x] Review: independent review of identifier boundaries, API contract and save compatibility; correct load-bearing findings.
- [x] Root: full fresh GCC build/ctest, desktop/V2 checks, Visual Studio project generation/open, final diff and uncommitted-state verification before deadline.

## Execution Ledger

- 19:21: renamed branch, synchronized fa43732 and updated local main. New remote branch pointer was published before the user's later no-submission instruction; no refactoring commits have been made.
- Baseline tests use a separate ignored immutable source snapshot so concurrent refactoring cannot invalidate the baseline.

- 20:25: final GCC Debug full regression 5746/5746 passed; final MSVC CLI/Web/tests rebuilt successfully. MSVC whole-suite environment comparison also passed 5746/5746. Desktop 235, V2 contract 67, isolated HTTP 164 passed.
- 11 module README files and exact HTTP/C++ API indexes delivered and included in Visual Studio. Multiplayer production files restored at original paths and verified identical to HEAD. HEAD remains fa43732; no commits or refactor pushes.
