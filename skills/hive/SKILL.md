---
name: hive
description: Parallel multi-agent execution for Claude Code. Use when a request is big enough to split across several agents working at once, such as building a project or feature that spans many files, bulk edits, migrations or test-writing across a codebase, parallel research, or when the user says "swarm", "use multiple agents", "in parallel", "hive" or "faster". Plans a dependency graph with exclusive file ownership, dispatches worker subagents in parallel waves with a per-task intelligence dial (haiku, sonnet, opus or the session's own model), and keeps token usage near-flat with on-disk briefs and one-line reports.
---

# Hive: parallel agents without the token blow-up

You are the **orchestrator**. You plan once, then a deterministic kernel (`hive.py`) handles
scheduling, state and briefs, so neither you nor the workers spend tokens on bookkeeping. Workers
read their assignment from disk and reply with one line. Your context grows by about 150 tokens
per task, however much code the workers write.

```
you ──plan.md──▶ hive.py dispatch ──▶ N parallel Agent calls (40-token prompts)
                                         │ each worker: hive.py brief <id> → work → hive.py done <id>
you ◀── one line per worker ◀────────────┘   state lives in .hive/status/*.json, not in your context
```

## Use it or skip it

- **Use it** for 3 or more units of work that can proceed independently, or about 6 or more files to create or change.
- **Skip it** for single-file fixes, quick questions, or tightly coupled step-by-step debugging. Do those directly: a worker costs about 15k tokens of overhead.
- Only the main session can dispatch. If you are already a subagent, work through the plan sequentially using `brief`.

## Protocol

Set `K` to this skill's `scripts/hive.py` (the skill's base directory is shown when the skill loads).
After `init`, everything uses the project-local copy `python3 .hive/bin/hive.py` (written `H` below).

1. **Recon, cheaply.** Learn the repo with Glob, Grep and targeted Reads. For a large unknown codebase,
   make wave 0 of the plan `scout` tasks that each write findings to `.hive/findings/<area>.md`.
2. **Init:** `python3 K init --goal "<goal>" --budget balanced` (budgets: `lean`, `balanced`, `max`).
3. **Write `.hive/context.md`**, the one brief every worker reads: stack, conventions, layout, contracts
   and the test command. Keep it under about 1,500 words, because every worker pays for it.
4. **Write `.hive/plan.md`** using the format below. Follow [references/planning.md](references/planning.md) for decomposition:
   contracts first, **exclusive file ownership**, tasks of 1–6 files, a self-sufficient spec and an `accept:` command for each task.
5. **Run `H validate`** and fix every ERROR. It catches cycles, unknown deps and two tasks that could write the same file at once.
   Run `H estimate` when the user cares about cost.
6. **Dispatch loop.**
   - Run `H dispatch`. It marks the ready tasks running and prints one Agent call for each (the tool is called `Agent`, or `Task` in older versions).
   - **Send every printed call in a single message**, copying `description`, `subagent_type`, `model` and `prompt` exactly.
     Never add context to a prompt: the worker loads its own brief.
   - When workers return, run `H dispatch` again. If agents run in the background, run it after **each**
     completion so newly unblocked work starts immediately (streaming).
   - Repeat until it prints `ALL DONE`.
7. **Steer, don't redo.** Check `H status` between waves.
   - **Requests** (a worker needed a change in a file it doesn't own): append a follow-up task to plan.md with a new id and the right `deps`, then dispatch.
   - **FAIL:** read the note, then fix the spec, split the task, or raise its tier or model. Run `H reset <id>`, then dispatch.
   - **Stuck in running** (the worker died without reporting): `H dispatch --requeue` once no agents are in flight.
   - You can edit plan.md mid-run to add tasks. Finished tasks keep their state.
8. **Verify:** run `H verify --run`. Fix small failures yourself. For larger ones, add `verifier` or `builder` fix tasks and dispatch them.
9. **Report** to the user in a few lines: what was built, what passed, and anything left open. Don't paste code.

## Scaling to tens, hundreds, thousands of tasks

The kernel validates and dispatches a 3,000-task plan in about 0.1 s. What limits scale is how
many workers run at once and each worker's fixed overhead, so use these features:

- **`foreach:` templates.** Write one entry and it expands to N tasks, so you never hand-write 100 entries.
  `foreach: glob:src/**/*.c`, `foreach: range:1..40` or `foreach: auth, users, billing`.
  The placeholders `{item} {name} {stem} {dir} {slug} {i}` are substituted into the id, the title, `writes`, `reads`, `accept` and the spec.
  A task that `deps:` on the template id waits for **every** copy (fan-in).
- **Packing (`pack: N`).** One worker runs up to N small tasks back to back, so it pays the roughly 15k overhead once.
  Lanes only grow as big as needed to launch every ready task now, so parallelism is kept.
  `architect`, `integrator` and `judge` tasks always run alone. Use `pack: 3` to `pack: 6` for swarms of small tasks.
- **`max_parallel`.** Raise it for big swarms (20–50) if your environment allows that many concurrent subagents.
  Lanes beyond the limit start as slots free up.
- **Compact dispatch.** When there are more than 6 calls, dispatch prints the prompt template once plus one line per call.
  Build each prompt by replacing `<IDS>` in the template with that lane's ids.
- **Project roots.** A plan can live in a subdirectory (`demo/.hive/`), and all its paths are relative to `demo/`.
  Workers are told this in their brief.
- **Wide beats deep.** Keep the critical path short: one contracts wave, one wide build wave, then integrate.
  Each extra dependency level adds a full worker round-trip to the wall-clock time.

## Token discipline (why cost stays flat)

- A dispatch prompt is about 40 tokens. **Never** inline file contents, context or the plan into it.
- Don't read the files workers wrote just to check them. Trust `done` plus `verify`, and read only what failed.
- Shared facts go in context.md **once**, not repeated in every spec. A spec states only what is unique to its task.
- Set the tier to the difficulty. Most tokens are spent in workers, so choosing `fast` for grunt work is the biggest saving.
- `xN` replicas multiply cost by N+1. Save them for pieces where being wrong is expensive.
- Details and numbers: [references/economics.md](references/economics.md).

## Intelligence dial

| tier       | lean   | balanced (default) | max    | use for |
|------------|--------|--------------------|--------|---------|
| `fast`     | haiku  | haiku              | sonnet | docs, configs, fixtures, boilerplate, scouting, repetitive files |
| `standard` | sonnet | sonnet             | opus   | ordinary feature code and tests |
| `deep`     | sonnet | opus               | opus   | architecture, hard algorithms, integration, judging |
| `inherit`  | the session's own model (full orchestrator intellect) | | | anything you'd want done exactly as you would do it |

Each role picks a sensible default tier. You can override it per task, for example `[builder deep]`, or pin a model with `[builder opus]`.
`budget: max` plus `inherit` everywhere means every worker gets your own intellect.

## Modes (plan shapes, not switches)

- **Swarm:** many independent tasks with no deps. Use for bulk work such as "tests for 30 modules" or "translate 12 pages".
- **Unite:** many workers on **one** goal. The architect writes contracts, builders fan out, then an integrator fans in, and a verifier checks the result.
- **Pipeline:** staged waves (scouts → architect → builders → integrator → verifier).
- **Consensus:** `[builder x3]` with `variants: a | b | c` produces 3 independent candidates, and a judge picks or merges them into the real path.
- **Recon:** scouts only. You read their findings, then plan the real work.

Mix them freely in one plan. Examples: [references/modes.md](references/modes.md).

## plan.md format

```markdown
goal: One-sentence goal
budget: balanced          # lean | balanced | max
max_parallel: 8           # optional; default comes from the budget (4 / 8 / 12)
agents: general           # general | hive | plugin (see Worker types)
pack: 1                   # optional; >1 lets one worker run up to N small tasks
accept: npm test          # optional global check for verify --run (repeatable)

## T1 [architect] Shared contracts
writes: src/types.ts, src/api/contract.ts
reads: README.md
Everything after the key lines is the spec: what to build, the exact names, edge cases.

## T2 [builder] Users API
writes: src/api/users.ts
deps: T1
accept: npx vitest run src/api/users
Implement GET/POST /users against the contract in src/types.ts.

## T3 [scribe fast] Docs
writes: docs/
deps: T1
Document the public API.

## TEST-{stem} [builder] Tests for {name}
foreach: glob:src/api/*.ts
writes: tests/{stem}.test.ts
reads: {item}
deps: T2
Write tests for {item}.
```

The header is `## <ID> [role tier|model xN] Title`, and everything inside `[...]` is optional.
Key lines are `writes:`, `reads:`, `deps:` (comma separated), `accept:` (repeatable), `variants:` (`|` separated) and `foreach:`.
A path ending in `/` owns the whole directory. Use `###` headings inside specs, never `##`.
Roles: `architect`, `builder`, `scout`, `scribe`, `verifier`, `integrator` and `judge` (or a custom name).
Full reference: [references/plan-format.md](references/plan-format.md).

## Worker types

The default, `agents: general`, uses `general-purpose` subagents, which works everywhere because the brief carries the role.
If the hivemind agents are installed, `agents: hive` (standalone install) or `agents: plugin` (plugin install)
switches to the `hive-<role>` subagents. Those restrict tools (scouts are read-only) and preset models.

## Kernel commands

`init`, `validate`, `waves`, `estimate [--pack N]`, `dispatch [--dry --json --max N --pack N --verbose --requeue --stale MIN --retry-failed]`,
`status [--full]`, `board`, `verify [--run]`, `reset <ids>|--failed|--all`.
Worker-side: `brief <id>[,<id>...]`, `done <id> -m`, `fail <id> -m`, `ask <id> "<path>: change"`, `post <id> "decision"`.
