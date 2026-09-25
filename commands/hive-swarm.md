---
description: Flat swarm — one task (or small batch) per independent file/module, no dependencies.
argument-hint: <task description>
---

Load the `hive` skill via the Skill tool if it is not already loaded, and follow its protocol.

The swarm task is: $ARGUMENTS

If no task text is given, ask the user what to swarm over before doing anything else.

1. Use Glob (and Grep if needed) to enumerate the independent items the task applies to — files or
   modules that can each be handled without touching another item's files.
2. `python3 <skill-base>/scripts/hive.py init --goal "<task>" --budget balanced` (adjust budget only
   if the user asked for lean/max).
3. Write `.hive/context.md` with the shared brief (conventions, the pattern every item should follow).
4. Write `.hive/plan.md` as a flat swarm: one `[builder]` task per item, or a small batch of closely
   related items per task when there are many (dozens), each `writes:` only its own file(s), no `deps:`,
   tier `fast` for boilerplate/repetitive items or `standard` for ordinary code. No architect stage —
   this mode is for independent, already-well-understood items, not ones needing shared contracts.
5. `H validate`, then run the dispatch loop until `ALL DONE`.
6. `H verify --run`, then report results in a few lines.
