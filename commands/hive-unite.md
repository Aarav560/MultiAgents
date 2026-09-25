---
description: Many agents converging on one deliverable — contracts, parallel builders, integration, verification.
argument-hint: <goal>
---

Load the `hive` skill via the Skill tool if it is not already loaded, and follow its protocol.

The goal is: $ARGUMENTS

If no goal text is given, ask the user for it before doing anything else.

Plan a pipeline, not a flat swarm:

1. `python3 <skill-base>/scripts/hive.py init --goal "<goal>" --budget balanced`.
2. Write `.hive/context.md` with the shared brief.
3. Write `.hive/plan.md`:
   - One `[architect deep]` task first: it produces the shared contracts (types, interfaces, file
     layout) every builder codes against, with no deps.
   - Several `[builder]` tasks in parallel, each `deps: <architect id>`, each owning a disjoint set
     of files, each spec referencing the contract by name.
   - If one piece is unusually risky or hard to get right, make that task `[builder x3]` with
     `variants: a | b | c` describing three different approaches, plus a `[judge]` task with the
     same id-family that depends on all three replicas and picks or merges the result.
   - One `[integrator]` task depending on all builders (and the judge, if used) that wires the
     pieces together and resolves any seams the builders couldn't see individually.
   - One `[verifier]` task depending on the integrator that runs the acceptance checks end to end.
4. `H validate`, then run the dispatch loop wave by wave until `ALL DONE`, steering on `ask`/`post`
   and failures between waves.
5. `H verify --run`, then report to the user: what was built, what passed, what's left open.
