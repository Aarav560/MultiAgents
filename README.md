# hivemind

Parallel multi-agent execution for Claude Code. One session plans the work once, then many
subagents build it at the same time, each owning its own files. A small stdlib Python kernel,
`hive.py`, handles scheduling, state and briefs on disk. The orchestrator sends each worker a
prompt of about 40 tokens and gets back one line. Its context grows by about 150 tokens per task,
however much code the workers write, so a 40-file project costs close to what a single agent would
spend while finishing in a fraction of the wall-clock time.

## How it works

```
             ┌───────────────────────────────┐
  you ──────▶│ orchestrator (main session)   │ writes .hive/context.md + .hive/plan.md once
             └──────────────┬────────────────┘
                            │ hive.py validate   (cycles, unknown deps, write conflicts)
                            │ hive.py dispatch   (ready tasks, critical path first, <= max_parallel)
          ┌─────────────────┼─────────────────┐
          ▼                 ▼                 ▼
     worker T2         worker T3         worker T4       ~40-token prompt each, one message
     hive.py brief     hive.py brief     hive.py brief   reads context.md + its own spec
     edits its files   edits its files   edits its files exclusive `writes:` ownership
     hive.py done      hive.py done      hive.py done    .hive/status/T2.json, T3.json, ...
          │                 │                 │
          └──── one line ───┴──── one line ───┘
                            ▼
             dispatch again ... ALL DONE ... hive.py verify --run
```

Tasks form a dependency graph. Each task has a role, a tier, exclusive `writes:` ownership,
`deps:`, `reads:`, `accept:` commands and a spec. Workers coordinate through `ask` (a change request
for a file they don't own) and `post` (a team board shown in later briefs).

## Install

### Plugin marketplace (recommended)

```
/plugin marketplace add aarav560/multiagents
/plugin install hivemind@multiagents
```

This installs the `hive` skill, the seven `hive-*` subagents, the slash commands and a
SessionStart hook that prints a one-line progress summary when a run is in progress.

### Standalone installer

```bash
git clone https://github.com/aarav560/multiagents.git
cd multiagents
./install.sh              # copies skills/hive, agents/hive-*.md and commands/ into ~/.claude
./install.sh --uninstall  # removes exactly what it copied
```

### Project-level

```bash
cd /path/to/your/project
/path/to/multiagents/install.sh --project   # installs into ./.claude instead of ~/.claude
```

Commit `./.claude` if your whole team should get the skill. The installer requires `python3` and
must run from a clone, because it resolves paths relative to itself.

## Quick start

```
/hive build a REST API for todos with auth, tests and docs
```

What happens:

1. **Recon.** The orchestrator learns the repo with Glob, Grep and targeted Reads. On a large
   unfamiliar codebase, wave 0 becomes `scout` tasks that write to `.hive/findings/<area>.md`.
2. **Init.** `hive.py init --goal "..." --budget balanced` creates `.hive/` and copies the kernel to
   `.hive/bin/hive.py`.
3. **Plan.** It writes `.hive/context.md` (the shared brief, under about 1,500 words) and
   `.hive/plan.md` (the task graph).
4. **Validate.** `hive.py validate` must pass with no ERROR lines.
5. **Dispatch loop.** `hive.py dispatch` marks ready tasks running and prints one call per task for
   the Agent tool (named Task in older Claude Code versions). All calls go out in one message, so
   they run in parallel. When workers return, it dispatches again, until `ALL DONE`.
6. **Steer.** Between waves it reads `status`, turns `ask` requests into follow-up tasks, and
   resets and re-specs failed tasks.
7. **Verify and report.** `hive.py verify --run` checks owned files and runs every `accept:`
   command. You get a few lines: what was built, what passed, what is open.

Flags: `/hive --lean`, `/hive --max`, `/hive --inherit`, and `/hive --dry` (plan, validate and
estimate, then stop before dispatching).

## Intelligence dial

Each task gets a tier. The plan's budget maps tiers to models.

| tier       | lean   | balanced (default) | max    | use for |
|------------|--------|--------------------|--------|---------|
| `fast`     | haiku  | haiku              | sonnet | docs, configs, fixtures, boilerplate, scouting |
| `standard` | sonnet | sonnet             | opus   | ordinary feature code and tests |
| `deep`     | sonnet | opus               | opus   | architecture, hard algorithms, integration, judging |
| `inherit`  | session model | session model | session model | work you want done exactly as you would do it |

Default `max_parallel` is 4 / 8 / 12 for lean / balanced / max. Override a tier per task
(`[builder deep]`) or pin a model (`[builder opus]`).

## Modes

Modes are plan shapes, not switches. Mix them in one plan.

| mode      | shape | good for |
|-----------|-------|----------|
| Swarm     | many independent tasks, no deps | tests for 30 modules, translating 12 pages |
| Unite     | architect → parallel builders → integrator → verifier | one feature across many files |
| Pipeline  | staged waves: scouts → architect → builders → integrator → verifier | migrations, rewrites |
| Consensus | `[builder x3]` + `variants: a \| b \| c`, then a judge | pieces where a wrong answer is expensive |
| Recon     | scouts only, you plan after reading findings | unfamiliar codebases |

Replicas `xN` expand to candidate tasks `ID.r1..rN` that write under `.hive/candidates/ID/rK/`,
plus a judge that keeps the original id and writes the real paths. Examples of every shape:
[skills/hive/references/modes.md](skills/hive/references/modes.md).

## Commands

Slash commands (from `commands/`):

| command | what it does |
|---------|--------------|
| `/hive [--lean\|--max\|--inherit] [--dry] <goal>` | full protocol: plan, validate, dispatch, verify, report |
| `/hive-swarm <task>` | flat swarm, one task per independent file or module |
| `/hive-unite <goal>` | contracts, parallel builders, integrator, verifier |
| `/hive-status` | three-line summary of the active run |
| `/hive-resume` | resume after a restart (`dispatch --requeue`, then continue) |

Kernel commands (`python3 .hive/bin/hive.py <command>`):

| command | what it does |
|---------|--------------|
| `init [--goal G] [--budget lean\|balanced\|max] [--force] [--track]` | create `.hive/`, templates and a local kernel copy |
| `validate` | DAG checks, cycles, unknown deps, write conflicts, size warnings |
| `waves` | tasks by wave with model assignment |
| `estimate [--full]` | rough token and wall-clock estimate |
| `dispatch [--dry] [--json] [--max N] [--requeue] [--stale MIN] [--retry-failed]` | mark ready tasks running, print Agent calls |
| `status [--full] [--line]` | progress view (`--line` is one line, silent when idle, for hooks) |
| `board` | print the team board |
| `verify [--run] [--timeout SEC]` | check owned files exist; `--run` executes `accept:` commands |
| `reset <ids> \| --failed \| --all` | return tasks to pending |
| `brief <id> [--peek]` | (worker) print a task's full assignment |
| `done <id> -m "note" [--force]` | (worker) record success |
| `fail <id> -m "note"` | (worker) record failure |
| `ask <id> "<path>: change"` | (worker) request a change in a file it does not own |
| `post <id> "decision"` | (worker) post a decision to the board |

## plan.md example

```markdown
goal: Todo API with auth
budget: balanced
agents: general
accept: npm test

## T1 [architect] Shared contracts
writes: src/types.ts, src/api/contract.ts
Define Todo, User and the request/response types for every route.

## T2 [builder] Todos API
writes: src/api/todos.ts
deps: T1
accept: npx vitest run src/api/todos
Implement GET/POST/PATCH/DELETE /todos against src/api/contract.ts.

## T3 [builder x3] Auth middleware
writes: src/auth.ts
deps: T1
variants: JWT | session cookie | API key
Verify the caller and attach `user` to the request.

## T4 [scribe fast] Docs
writes: docs/
deps: T1
Document every route in docs/api.md.
```

The header is `## <ID> [role tier|model xN] Title`. A path ending in `/` owns the whole directory.
Use `###` inside specs, never `##`. `agents: hive` (standalone install) or `agents: plugin` (plugin
install, `hivemind:hive-<role>`) switches workers to the tool-restricted `hive-*` subagents. Full
format: [skills/hive/references/plan-format.md](skills/hive/references/plan-format.md). A realistic
13-task, 44-file plan: [examples/saas-api](examples/saas-api/README.md).

## Scaling to hundreds of tasks

The kernel validates and dispatches a 3,000-task plan in about 0.1 s. These features keep large runs cheap and fast:

| feature | what it does |
|---|---|
| `foreach: glob:src/*.c` / `range:1..50` / `a, b, c` | one plan entry expands to N tasks; `{item} {name} {stem} {dir} {slug} {i}` placeholders; deps on the template fan in to every copy |
| `pack: N` | one worker runs up to N small tasks back to back, paying the ~15k overhead once; lanes are spread evenly over free slots so packing never costs parallelism |
| `max_parallel: 20+` | wide waves; lanes beyond the limit start as slots free up |
| compact dispatch | batches over 6 calls print the prompt template once plus one line per call |
| project roots | a plan in `demo/.hive/` resolves every path against `demo/` |
| critical-path first | long dependency chains start before short ones |

## Case study: a space simulator in C, built by 27 agents

[`demos/spacesim`](demos/spacesim) is `orbit`, an N-body simulator in C11 built by hive from one plan. It has
direct and Barnes-Hut gravity, 5 integrators (up to 4th-order symplectic Yoshida), Keplerian orbital mechanics,
Hohmann transfers, inelastic collisions, 7 scenarios (from the solar system to a 1,500-particle disk galaxy), CSV,
terminal and PPM output, a CLI and INI configs.

- **The plan:** the orchestrator wrote the core headers, `world.c`, the test harness and a complete API contract
  ([`.hive/context.md`](demos/spacesim/.hive/context.md)), then a 26-task plan ([`.hive/plan.md`](demos/spacesim/.hive/plan.md)).
  Modules are decoupled through an `accel_fn` function pointer, so 22 tasks ran in the first wave, on
  20 concurrent workers (haiku, sonnet and opus picked per task).
- **Mid-run steering:** the benchmark showed Barnes-Hut barely beating direct summation, so the orchestrator
  appended an optimization task to the live plan and dispatched it alongside the integration work.
- **Result:** about 7,500 lines of C across 67 files. 15 test suites (5,400+ checks) and a 24-check scenario sweep pass,
  and everything compiles clean with `-Wall -Wextra -Wpedantic -Werror`. The figure-8 three-body orbit conserves energy to 1e-15
  over a full period, and the Hohmann transfer lands 4.7 km from the 42,164 km GEO target.

```
cd demos/spacesim && make && make test && ./build/orbit --scenario solar --ascii
```

## Token economics

Every worker pays a fixed overhead of about 10-15k tokens (system prompt and tools), plus its
brief, the files it reads, and its output. hivemind keeps the rest flat:

- Dispatch prompts are about 40 tokens. Workers load their own brief from disk.
- `context.md` is written once and read by each worker, never pasted into prompts.
- Workers reply with one line. State lives in `.hive/status/`, not in the orchestrator's context.
- Tiers put grunt work on haiku, where most tokens are spent.
- Fewer, larger tasks (up to about 12 owned files) beat many tiny ones, because overhead is per task.

In the worked 40-file example, a naive 10-agent swarm spends about 290k input tokens. hivemind at
the balanced budget spends about 173k, within about 1.6x of a single agent, and finishes in roughly
a quarter to a third of the time. Run `hive.py estimate` on your own plan, and tune
`agent_overhead_tokens` in plan.md (default 14000). Details:
[skills/hive/references/economics.md](skills/hive/references/economics.md).

## Safety model

- **Exclusive ownership.** Each task lists the files it may write. Workers are told to touch
  nothing else and to use `ask` for changes elsewhere.
- **Conflicts caught before dispatch.** `validate` fails if two tasks can write the same path
  (including directory ownership) and neither depends on the other.
- **Done guard.** `done` refuses to record success while any owned file is missing, unless the
  worker passes `--force`.
- **One file per status.** Each task's state is its own JSON file in `.hive/status/`, so parallel
  workers never contend for a lock or overwrite each other.
- **No git operations by workers.** Workers edit files and nothing else. The kernel never runs git,
  and by default `.hive/.gitignore` ignores all run state. Committing, branching and reverting stay
  with you and the orchestrator. `init --track` lets git track `plan.md` and `context.md`.

## FAQ

**Does it work without the plugin?**
Yes. `./install.sh` installs the same skill, agents and commands without the marketplace. You can
also run the kernel directly: `python3 skills/hive/scripts/hive.py init`, write the plan, then
`python3 .hive/bin/hive.py dispatch` and paste the printed calls yourself. The default
`agents: general` uses `general-purpose` subagents, which exist everywhere.

**Can subagents spawn subagents?**
No. Only the main session can dispatch. If a task is too big for one worker, split it in plan.md.
If the skill loads inside a subagent, it works through the plan sequentially with `brief`.

**What if a worker dies?**
Its task stays `running`. Once no agents are in flight, `dispatch --requeue` returns it to pending,
and `dispatch --stale MIN` requeues only tasks running longer than MIN minutes. After a restart,
`/hive-resume` does this for you. Failed tasks can be fixed and rerun with `reset <id>` or
`dispatch --retry-failed`.

**How do I give workers my own intellect?**
Use tier `inherit`, which omits the model so each worker runs on the session's model. `/hive
--inherit <goal>` applies it to every task. `budget: max` raises every other tier as well.

**Windows?**
The kernel is stdlib Python and state is plain files, so it runs wherever `python3` does. The hook
calls `python3`, so it must be on your PATH. `install.sh` needs bash (Git Bash or WSL). Without it,
use the plugin marketplace, or copy `skills/hive`, `agents/hive-*.md` and `commands/` into
`%USERPROFILE%\.claude` by hand.

## Repository layout

```
.
├── .claude-plugin/
│   ├── plugin.json            plugin manifest (hivemind)
│   └── marketplace.json       marketplace entry (multiagents)
├── agents/                    hive-architect, -builder, -scout, -scribe, -verifier, -integrator, -judge
├── commands/                  hive, hive-swarm, hive-unite, hive-status, hive-resume
├── hooks/hooks.json           SessionStart: hive.py status --line
├── skills/hive/
│   ├── SKILL.md               orchestrator protocol
│   ├── scripts/hive.py        the kernel (stdlib Python)
│   └── references/            planning, plan-format, modes, economics
├── examples/saas-api/         40-file example plan (context.md, plan.md, README.md)
├── demos/spacesim/            orbit: C11 N-body simulator built by 27 hive agents (plan in .hive/)
├── tests/test_hive.py         kernel tests (python3 -m unittest discover -v tests)
├── install.sh                 standalone installer
└── LICENSE
```

## License

MIT. See [LICENSE](LICENSE).
