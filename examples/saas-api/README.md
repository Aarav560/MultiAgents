# Example: 40-file SaaS API project plan

A realistic hive plan for a multi-tenant SaaS API: auth, orgs, users, projects, billing
webhooks and per-org rate limiting, built with Fastify + Zod + Vitest. 13 tasks (16 once the
rate-limiter's `x3` consensus expands into 3 candidates + a judge), 44 owned files, 5 waves.

This directory holds only the **plan** (`context.md`, `plan.md`) and this README. Running the
plan through `hive.py` creates the actual TypeScript project in a separate demo directory — it
does not write into this repo.

## Run it

From the repo root:

```bash
mkdir demo && cd demo
python3 ../skills/hive/scripts/hive.py init --goal "Multi-tenant SaaS API"
cp ../examples/saas-api/context.md ../examples/saas-api/plan.md .hive/
python3 .hive/bin/hive.py validate
python3 .hive/bin/hive.py estimate     # optional: rough token/wall-clock cost
```

Then either:

- Say **`/hivemind:hive`** (plugin install) or **`/hive`** (standalone install) in this Claude Code session (with the demo/ directory as your working
  directory) and let the orchestrator run the dispatch loop, or
- Drive the loop yourself:

```bash
python3 .hive/bin/hive.py dispatch
```

Copy every printed `[n] description / subagent_type / model / prompt` line into one Agent tool
message (call the Agent tool, named Task in older Claude Code versions, once per line, all in the
same turn) so the wave's tasks run in parallel. When the workers reply, run `dispatch` again —
repeat until it prints `ALL DONE`, then run:

```bash
python3 .hive/bin/hive.py verify --run
```

## Expected `validate` output

Right after `init` + copying the two files in, before any task has run:

```
OK 13 tasks, 50 owned paths, 5 waves, max_parallel 8, parallel speedup up to ~2.5x vs serial
```

(`validate` counts tasks after `xN` expansion — the 13 tasks in `plan.md` become 13 visible
task ids, but `T6`'s `x3` adds 3 candidate writers, so `waves`/`status` show 16 rows; "owned
paths" similarly counts the candidates' mirrored paths under `.hive/candidates/T6/`, not just
the 44 real project files. Exact numbers can drift slightly as the plan is tuned — treat the
shape, not the digits, as the contract: zero `ERROR` lines, at most a couple of `warn` lines,
and a `OK <n> tasks, <m> owned paths, <w> waves, ...` summary line.)

## Shape of the plan

| Wave | Tasks | What happens |
|------|-------|--------------|
| 0 | `T1` (architect, deep) | Domain types and Zod contracts every other task codes against. |
| 1 | `T2`-`T7`, `T9`, `T10` (builders + fast scribes) | One task per domain (auth, orgs+users, projects, billing), the rate limiter (`x3` consensus), the in-memory store, config/utils, and test fixtures — all parallel, all depending only on `T1`. |
| 2 | `T6` judge, `T8` (builder) | The judge picks the best rate-limiter candidate; middleware (auth/tenant/error handling) is built once JWT (`T2`) and the store (`T7`) exist. |
| 3 | `T11` (scribe), `T12` (integrator, deep) | Docs are written once the routes exist; the integrator wires everything into `app.ts`/`server.ts` and owns `package.json`/`tsconfig.json`/`vitest.config.ts`. |
| 4 | `T13` (verifier) | Runs `npx tsc --noEmit` and `npx vitest run` end to end. |

Every one of the ~40 files listed in `context.md`'s layout is owned by exactly one task —
`validate` fails the plan otherwise, so this is enforced, not just documented.

## Notes

- `plan.md` sets a plan-level `accept: npx tsc --noEmit` and `accept: npx vitest run`, so
  `verify --run` exercises the whole suite once, in addition to each task's own narrower
  `accept:` command (e.g. `npx vitest run tests/auth`).
- The demo project itself has no external DB or JWT library: persistence is in-memory
  `Map`-backed repositories and JWTs/webhook signatures are hand-rolled HMAC-SHA256 via
  `node:crypto`, so the plan stays runnable with just `npm install` and no external services.
- If you re-run this plan, `python3 .hive/bin/hive.py reset --all` clears state without
  touching the generated source files.
