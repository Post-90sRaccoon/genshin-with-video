# Playback Speed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add persistent playback speed controls to the native Bilibili overlay.

**Architecture:** Reuse libmpv speed and pitch correction. Store a preset in Settings, apply it on each load, expose it through a toolbar menu and context submenu.

**Tech Stack:** C++17, Win32, libmpv, existing PowerShell build/tests.

**Spec:** `docs/superpowers/specs/2026-11-05-playback-speed-design.md`

## Global Constraints

- Presets: 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2; default 1; reject unsupported/nonfinite values.
- Keep media position, pause intent, per-CID history and the wall-clock expiry policy.
- No extra dependency. Work on the requested branch in the current directory.

## Review Focus

- Old/corrupt settings: default to 1; config tests cover absent, null, string, out-of-range and nonfinite speed.
- Speed changes while paused: no playhead jump or reload; controller tests cover pause and entry identity.
- Loading/reloading: use the newest saved speed; controlled load-result and renewal tests cover this.
- Minimum width and DPI: six buttons must fit; native layout test checks bounds at small widths and scaled DPI.
- Menu selection: displayed radio/button labels must match the speed that is actually persisted and applied.

## Task 1: Persistent speed controls and verified portable build

**Files:** `src/core.h`, `src/core.cpp` (preset validation); `src/dialogs.h`, `src/dialogs.cpp` (settings); `src/player.h`, `src/player.cpp` (speed API/snapshot); `src/main.cpp` (native controls); `tests/app_tests.cpp`, `tests/integration.cpp`; README and verification docs.

**Interfaces:** `PLAYBACK_SPEEDS`, `valid_playback_speed(double)`; `Settings::speed`; `Player::set_speed(double)` and `Snapshot::speed`; `App::set_speed(double)`, `App::speed_menu()`, speed command IDs 200–206 and toolbar command SPEED.

- [x] Write config regression: `write_settings(read_settings({{"speed",1.5}})).value("speed",1.0)==1.5`. Run `scripts/build.ps1 -ControllerOnly`; observed the new assertion fail against unchanged code.
- [x] Add tests for invalid configuration, real menu command persistence, paused identity/position, measured half/double speed, reload and toolbar layout.
- [x] Implement the interfaces above. Apply saved speed in `accept_results`; use radio menu items and responsive toolbar widths.
- [x] Run the complete suite via `scripts/build.ps1`; 30 core assertions, offline integration and controller tests pass. Diagnostics regression passes.
- [x] Extend network integration to accept a speed argument and exercise live rate changes while preserving pause, media identity and playback progress.
- [x] Build the portable app with `scripts/build.ps1`, verify actual Bilibili playback and record observed results. Desktop automation initialization failed; real native menu/layout regressions provide the UI checks for this run.
- [x] Independent whole-change code review found no blockers; final diff and binary hashes verified.

Ruling: implement directly in the new branch using the existing dependency/build tree. Keep unrelated untracked investigation/plan files intact. This request covers the feature and local runnable build; follow the user's next instruction for committing or publishing it.

## Task 2: Configurable global speed shortcuts

- [x] Red: five-entry hotkey config migrates to eight, preserving custom bindings and resolving new-default collisions; eight-entry config roundtrips. Observed failure against old five-entry code.
- [x] Expand settings/resources/registration to eight actions. Defaults Ctrl+Alt+[ / ] / 0; add stepping and WM_HOTKEY actions 505/506/507.
- [x] Exercise real hotkey message dispatch, limits/reset, modal/hidden/paused behavior, settings resource fields and conflict cleanup in controller regressions.
- [x] Build the portable app, run the complete regressions, review changes, and document observed results and shortcuts. Fixed the reviewed header dependency, confirmed by failed-then-green full builds.
