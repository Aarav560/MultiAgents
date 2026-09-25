# Token economics

Multi-agent runs don't save tokens by magic. They save **wall-clock time** by running work in
parallel, and hive's job is to keep the **token total** close to what one agent doing the same
work would spend, instead of letting it multiply with every added worker. This page shows where
the tokens actually go, why naive multi-agent setups blow that budget, and which lever in
`hive.py` closes each leak.

## The cost model

Every worker's input tokens are four things, added together:

1. **Fixed overhead** — system prompt, tool definitions, subagent boilerplate. About 10-15k tokens
   per worker no matter how small its task is. This is `agent_overhead_tokens` in plan.md, default
   `14000`. It is paid once per dispatched task and is the single biggest line item, which is why
   task granularity (how many files each task owns) matters more than almost anything else.
2. **The brief** — `context.md` (paid by every worker, so it is capped at ~1,500 words) plus the
   task's own spec. Typically 1-3k tokens.
3. **Files it reads** — whatever is listed under `reads:`, sized directly off disk.
4. **Its output** — the files it writes, counted once for the write and again on the assumption it
   re-reads its own output while editing (`hive.py estimate` uses `max(size, 900)` per file for
   this reason).

The **orchestrator's** cost per task is roughly 150 tokens: one ~40-token dispatch prompt plus one
~110-token one-line report. That is the whole point of the architecture — the orchestrator's
context grows by a fixed, tiny amount per task regardless of how much code gets written, because
it never reads the work, only the verdict.

## Why naive multi-agent explodes

Spin up N subagents without this discipline and costs grow worse than linearly:

- **Re-sending the whole conversation or plan to every agent.** If each worker's prompt carries
  the full plan, the running transcript, or prior agents' output, every dispatch pays for
  everything that came before it. Ten agents each fed an 8k-token history is 80k tokens spent
  before any of them writes a line of code.
- **Verbose reports flooding the orchestrator.** A worker that replies with paragraphs of
  explanation, diffs, or "here's what I did and why" adds that to the orchestrator's context — and
  because the orchestrator re-reads its own context every turn, that cost is paid again on every
  subsequent dispatch round, not once.
- **Agents re-exploring the repo.** Without a shared brief, each worker greps and reads its way to
  the same understanding of conventions, layout and contracts that the previous nine workers
  already paid for.
- **Everyone on the biggest model.** Boilerplate, docs and fixtures don't need opus-tier
  reasoning, but a flat "use the strongest model for every agent" policy pays opus rates for
  haiku-shaped work.
- **N-way voting everywhere.** Running 3-5 independent candidates for every task multiplies cost
  by that factor for work where a single competent pass would have been fine.

## The levers hive uses

Each mechanism in `hive.py` closes one of the leaks above:

| Leak | Lever | Mechanism in `hive.py` |
|---|---|---|
| Full conversation resent per agent | On-disk briefs | `cmd_dispatch` prints a ~40-token `worker_prompt()`; the worker pulls its actual assignment itself via `cmd_brief`, which reads `context.md` + the task's own spec from disk. Nothing upstream is inlined into the dispatch call. |
| Verbose reports reread every turn | One-line replies | The brief's "Finish" section demands `<id> ok: <note>` or `<id> FAIL: <reason>` as the *only* reply; `cmd_done`/`cmd_fail` cap the stored note at 300 chars. |
| Downstream workers re-reading upstream output | Upstream notes, not full outputs | `cmd_brief` lists dependencies as `id title: note | files: a, b, c` — the one-line note and a file list, never the files' contents. |
| Cross-worker chatter growing unbounded | Board capped at 30 lines | `cmd_brief` includes only `BOARD.read_text(...).splitlines()[-30:]`; older decisions age out of every future brief automatically. |
| Paying for template comments in every brief | context.md comments stripped | `cmd_brief` runs `re.sub(r"<!--.*?-->\n?", ...)` on `context.md` before it goes into any brief. |
| Every task at opus rates | Tiered models | The `BUDGETS` table maps `fast`/`standard`/`deep` to `haiku`/`sonnet`/`opus` per budget, and each role has a sensible default tier (`scout`/`scribe` default `fast`, `builder`/`verifier` default `standard`, `architect`/`integrator`/`judge` default `deep`). |
| Idle waiting inflating wall-clock, which inflates re-dispatch overhead | Critical-path scheduling | `dispatch` sorts ready tasks by `critical_len()` (tier-weighted longest remaining path) so the tasks that gate the most downstream work start first within the `max_parallel` cap. |
| Silent partial work needing a costly re-run later | `done` refuses when owned files are missing | `cmd_done` checks every path in `writes:` exists before accepting the report; a worker can't mark done, vanish, and leave the orchestrator to discover the gap two waves later. |

## A worked example: 40 files

Estimates only; the arithmetic follows `hive.py`'s own model (`agent_overhead_tokens + brief + reads + outputs`, orchestrator `≈ 120 × tasks + 2000`).

| Approach | Rough input tokens | Rough output tokens | Wall-clock (relative) |
|---|---:|---:|---|
| Single agent, serial | 14,000 (overhead, once) + 40 × (1,500 read/context + 800 reasoning) ≈ **106,000** | 40 × 1,200 ≈ **48,000** | 1.0x (baseline, fully serial) |
| Naive swarm, 10 agents, no discipline | 10 × 14,000 (overhead) + 10 × 8,000 (resent plan/history) + 10 × 5,000 (repeat repo exploration) ≈ **270,000**, plus verbose reports reread across ~4 dispatch rounds: 10 × 500 × 4 ≈ **20,000** → **≈290,000** | 40 × 1,200 ≈ **48,000** | ~0.3x (parallel, but heavier per-call latency since most agents default to the strongest model) |
| Hive, balanced budget, 8 tasks (~5 files each) | 8 × 14,000 (overhead) + 8 × 1,900 (brief) + 8 × 1,200 (reads) ≈ **136,800**, plus outputs 40 × 900 ≈ **36,000** → **≈172,800**, plus orchestrator 8 × 120 + 2,000 ≈ **2,960** | **≈36,000** (same files, tiered models: docs/scaffolding on haiku, core logic on sonnet) | ~0.25-0.3x (3-4 waves in parallel, capped by `max_parallel`) |

Reading the table: the naive swarm costs ~2.7x the single agent *and* offers no token advantage
for the speed it buys. Hive lands within ~1.6x of the single agent's input tokens — the remaining
gap is `agent_overhead_tokens × tasks`, the one place hive spends more than a single agent would —
while matching the swarm's wall-clock win, because tiered models cut output cost, briefs replace
resent history, and one-line reports keep the orchestrator's side near-zero.

The lesson embedded in the arithmetic: **task count, not file count, drives overhead.** Doubling
task count for the same 40 files roughly doubles the overhead line without changing output tokens
at all. Fewer, larger tasks (`writes:` up to the ~12-file warning threshold in `validate`) cost
less; more, smaller tasks buy more parallelism. Pick the point that fits the deadline.

## Using `estimate`, and tuning `agent_overhead_tokens`

Run `H estimate` any time after `validate` passes:

```
$ python3 .hive/bin/hive.py estimate
haiku      3 workers  in~   45,200  out~  10,800
sonnet     5 workers  in~  102,400  out~  25,200
opus       2 workers  in~   38,600  out~   9,000
orchestrator overhead ~3,200 tokens (10 dispatch lines + one-line reports)
TOTAL ~189,400 in / ~45,000 out (rough; set agent_overhead_tokens to tune)
wall-clock: critical path 6 vs serial 18 weight units (~3.0x faster, capped by max_parallel 8)
```

`estimate --full` adds a per-task row (`id`, model, `in~`, `out~`) so you can spot the one task
whose `reads:` list is too generous or whose `writes:` list is too small to amortize overhead.

`agent_overhead_tokens` (plan.md key, default `14000`) is the fixed cost the estimator charges per
dispatched task before any brief, read or output. Tune it when your actual subagent runtime
differs from the default — a `general-purpose` subagent with a larger tool set, or a plugin
install where `hive-<role>` agents carry extra restricted-tool boilerplate. Lower it if your
workers run lean; raise it if real usage consistently exceeds the estimate. It doesn't change what
workers spend, only how honestly `estimate` predicts it — recalibrate from `H status --full` notes
or your own usage logs, not guesswork.

## Checklist: 10 rules for keeping token cost flat

1. Never inline file contents, the plan, or prior output into a dispatch prompt — the ~40-token
   `worker_prompt()` is the ceiling; the brief carries everything else.
2. Keep `context.md` under ~1,500 words; every worker pays for it once per task.
3. State each fact once, in `context.md`. A spec describes only what's unique to that task.
4. Match tier to difficulty: `fast` for docs/config/fixtures/boilerplate/scouting, `standard` for
   ordinary feature code, `deep` for architecture, integration and judging.
5. Prefer fewer, larger tasks (up to ~12 owned files) over many tiny ones — overhead is charged
   per task, not per file.
6. Reserve `xN` replicas for work where being wrong is expensive; each replica multiplies that
   task's cost by roughly N+1 (candidates plus a judge).
7. Never let a worker read files it has no reason to read; `reads:` is the minimum needed to code
   against the contract, not "the whole module for context."
8. Require one-line `done`/`fail` replies, no prose — the orchestrator's context grows by ~150
   tokens per task, not more.
9. Use `ask`/`post` instead of ad hoc cross-agent messages: `ask` records out-of-scope change
   requests without touching another worker's files; `post` keeps the board capped at 30 lines.
10. Run `H estimate` before a large dispatch and again after any plan edit that changes task
    count or file ownership — task-count changes are the biggest cost swing.
