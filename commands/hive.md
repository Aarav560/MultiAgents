---
description: Run the full hive protocol on a goal, from planning through dispatch to verification.
argument-hint: [--lean|--max|--inherit] [--dry] <goal>
---

Load the `hive` skill via the Skill tool if it is not already loaded, and follow its protocol exactly.

The goal is: $ARGUMENTS

Before anything else, parse leading flags off $ARGUMENTS (they may appear in any order, before the goal text):
- `--lean` sets `--budget lean` on `init`; `--max` sets `--budget max`. Default is `balanced`.
- `--inherit` means every task should use tier `inherit` (the session's own model) regardless of its role's default tier.
- `--dry` means: write context.md and plan.md, run `validate` and `estimate`, report the plan and cost estimate, and stop before dispatching.

Strip the flags from the remaining text to get the goal. If no goal text remains, ask the user for the goal before doing anything else — do not guess one.

Then:
1. Recon the repo cheaply (Glob/Grep/targeted Reads); use scout tasks in wave 0 if the codebase is large and unfamiliar.
2. `python3 <skill-base>/scripts/hive.py init --goal "<goal>" --budget <lean|balanced|max>`.
3. Write `.hive/context.md` and `.hive/plan.md` per the skill's format, applying `--inherit` to tiers if set.
4. Run `python3 .hive/bin/hive.py validate` and fix every error.
5. If `--dry`, run `estimate` and report; otherwise run the dispatch loop until `ALL DONE`, steering on requests/failures per the skill.
6. Run `verify --run`, then report to the user in a few lines: what was built, what passed, what's left open.
