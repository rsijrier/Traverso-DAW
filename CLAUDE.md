<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Instructions for agents working on Traverso

## Working with Remon

Remon is the architect. He decides on design, scope and release planning;
the agent implements within those decisions. Reports and questions are
shaped so he can stay in that role.

### Reports
- Start every report with at most five lines:
  1. what is done (commits, branch),
  2. what needs his decision,
  3. anything that went wrong or is uncertain.
- Details (test output, measurements, analysis) go to a file in reports/
  or doc/status.md, linked from the report, not pasted into it.
- Do not present your own work as finished or correct beyond what tests
  and reviews have shown. Say what was not checked.

### What always goes to Remon
Ask before deciding, however small the change looks, when it touches:
- threads: ownership, what is shared, which thread runs what, thread pools;
- new abstractions: interfaces, base classes, strategies, wrappers, flags
  for a second code path;
- disk or network access outside the known paths (everything goes through
  byteio and the ioscheduler);
- public APIs of a library;
- existing (older) code that a change gives a new role: a new caller, a
  new thread, new input;
- functionality that already exists elsewhere in Traverso: reusing it
  across a library boundary, moving it, or writing a second version of
  it;
- the recorded design rules, the scope of the task or the release
  planning.
When in doubt whether something belongs on this list: ask.

### What the agent decides itself
Implementation choices inside those boundaries. Record each one in the
decision log with a one-line reason.

### Decision log
- doc/decisions.md, one line per decision:
  date, branch, the choice, the reason, the files.
- Every Fable review reads the decision log of the branch and checks each
  entry against the recorded design rules.
- Before a merge, Remon reads the decision log of the branch. List the
  new entries in the merge report.

## doc/ and reports/

Anything a task, a decision or a design note refers to lives in doc/
and is committed. reports/ holds measurements, logs and working
reports only; nothing in doc/ may depend on a file in reports/.

## CMake linking

Every target links PRIVATE, and links every library it uses itself,
also when it only reaches that library through another library's
headers. No PUBLIC links, no exceptions. The purpose: each target's
CMakeLists.txt shows all its dependencies within Traverso.
