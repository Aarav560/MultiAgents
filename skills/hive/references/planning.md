# Decomposition playbook

How the orchestrator turns a goal into `.hive/plan.md`. Read this after `SKILL.md`; it expands
step 4 of the protocol. Everything here has to produce a plan that passes `hive.py validate`
with zero errors.

## 1. Five questions before you write a task

Answer these before you touch `plan.md`. Skipping one is where bad plans come from.

1. **What is the deliverable?** One sentence, concrete enough to test. "A todo API with auth" is
   not testable; "a REST API where `POST /todos` requires a bearer token and returns 201 with the
   created todo" is.
2. **Where are the seams?** Find the natural cut lines in the deliverable: modules, layers,
   directories, independent features. Seams are where two workers can touch different files
   without stepping on each other.
3. **What are the contracts across those seams?** Types, schemas, function signatures, route
   shapes, file formats. If two tasks both need to know the shape of a `Todo`, that shape must
   exist in a file before either task starts, or they will invent two different ones.
4. **What can run concurrently, and what can't?** Group tasks with no shared writes and no
   dependency between them into the same wave. Anything that reads another task's output waits
   for it via `deps:`.
5. **How will you prove each piece and the whole thing work?** Pick one cheap, deterministic
   command per task (`accept:`) and one for the whole plan (top-level `accept:` or a `verifier`
   task). If you can't name a command, the spec is probably too vague to hand to a worker.

## 2. Contracts-first

Wave 0 is almost always a single `architect` task that writes the shared contracts to real
files: `types.ts`, an OpenAPI or JSON-schema file, a `schema.sql`, a small `interfaces.py`. Not
prose describing the shapes — the actual file the compiler or the next worker will import.

Why a file and not a paragraph in `context.md`: everyone downstream needs a `reads:` entry that
resolves. `context.md` is paid for by every worker whether or not they need the detail; a
contract file is read only by the tasks that declare `reads: src/types.ts`, and it's the one
place all builders agree on, since Python and JS style disagreements about "what the User object
looks like" are exactly what parallel work makes expensive to discover late. It also means the
architect's tier can be `deep` (the one place in the plan where getting it wrong is costly) while
everything downstream can be `standard` or `fast`.

Every other wave-1+ task that needs the shape lists the contract file under `reads:` and never
under `writes:`. If two builders disagree about the contract mid-run, that's an `ask`, not a
second definition.

## 3. Ownership design

Every file has exactly one owner for the whole run — one task whose `writes:` covers it. `hive
validate` checks this by comparing every pair of tasks' `writes:` paths for overlap; it only
tolerates overlap when one task is an ancestor of the other (so the second edits after the first
finished, in sequence, not in parallel).

Rules of thumb:

- **Directory ownership.** `src/auth/` (trailing slash) hands one task the whole subtree. Prefer
  this over listing five files under `src/auth/` individually when the module is cohesive — it's
  fewer lines and it's forgiving if the worker needs one more file than you predicted.
- **Shared files go to one late integrator.** `package.json`, `index.ts` or `main.py`, a router
  or app-wiring file, a top-level config, a barrel/`index` re-export file — anything that every
  module touches — belongs to exactly one `integrator` task that `deps:` on all the builders that
  produced the pieces it wires together. It runs in the last or second-to-last wave, after the
  files it imports already exist.
- **Never split a shared file across two tasks in the same wave.** If you find yourself writing
  `writes: package.json` on two different builders, merge those into the integrator's job or into
  one builder, or `validate` will reject the plan outright.
- **Workers use `ask` for everything else.** If a builder discovers mid-task that it needs one
  line changed in a file it doesn't own, it calls `hive.py ask <id> "<path>: <change>"` and keeps
  going on its own files. The orchestrator turns real asks into a follow-up task between waves. Do
  not pre-emptively give a task write access to a file "just in case" — that's how two tasks end
  up owning the same path.

## 4. Task sizing

Target **1–6 files and one coherent concern** per task: a module, a route group, a test suite for
one area, one doc page. Two failure modes, both expensive:

- **Too small.** Each worker costs roughly 15k tokens of fixed overhead (brief, tool setup,
  report) before it writes a line of code. A task that produces one 20-line file wastes almost
  all of that. If you're describing two one-file tasks that both depend only on the same contract
  and nothing else, merge them.
- **Too big.** A task with 15 files and three unrelated concerns can't run in parallel with
  anything meaningful, becomes a serialization bottleneck (everything downstream waits on it),
  and is hard to spec precisely enough that the worker doesn't have to guess. `validate` warns
  past 12 owned files; treat that warning as "split this."

Rule of thumb for a mid-size job (about 40 files total): **8–12 tasks across 3–4 waves**. That
keeps most tasks in the 3–5 file range, keeps the critical path short (few forced serial hops),
and keeps the wave count low enough that dispatch/collect overhead doesn't dominate.

## 5. Writing a spec that stands alone

A worker reads exactly: the role description, `context.md`, your spec, its owned files, its
`reads:` files, and one-line notes from its upstream deps. It does not see other tasks' specs.
So the spec must be self-sufficient for *that task's unique work* — never "see T2's spec" or
"same pattern as above."

Include:

- **Exact names.** Function, class, route, table and field names — not "an endpoint for
  creating todos" but "`POST /todos` — request body `{title: string, done?: boolean}`, response
  `201 {id, title, done, createdAt}`."
- **Inputs and outputs.** Signatures, request/response shapes, return types, error shapes.
- **Edge cases that matter.** Empty input, auth failure, not-found, duplicate — whatever the spec
  needs to be correct, not an exhaustive QA checklist.
- **What NOT to do.** "Do not add a database migration tool," "do not touch `src/index.ts`," "no
  new dependencies." A boundary sentence is cheaper than a worker guessing wrong and creating a
  conflict.

Leave out anything shared: stack, style, test command, layout and contracts belong in
`context.md` once. `validate` warns when a spec exceeds ~6000 characters — that's usually a sign
material that belongs in `context.md` got pasted into every task instead of written once.

## 6. Acceptance per task

Pick one command per task that is cheap, deterministic, and fails loudly when the task is wrong.
Good examples: `npx tsc --noEmit src/auth/index.ts`, `npx vitest run tests/auth.test.ts`,
`python -m pytest tests/test_auth.py -q`, `node -e "require('./src/router.js')"`,
`python -c "import src.todos"`, `test -f docs/README.md`. Avoid full-suite commands per task
(slow, and they fail for reasons outside the task's own files); save those for the plan-level
`accept:` or a dedicated `verifier` task that runs after integration.

## 7. Tier selection

| tier | pick it for | example |
|---|---|---|
| `fast` | mechanical, low-risk, well-specified work | writing fixtures, a config file, a docs page, a scout's findings file, boilerplate CRUD following an established pattern |
| `standard` | ordinary feature code and tests | implementing one route module against a contract, writing a test suite for one area |
| `deep` | work where a mistake is expensive to unwind or that requires judgment across the whole system | writing the shared contracts, integrating parallel outputs into one app, judging replica candidates, a genuinely hard algorithm |
| `inherit` | anything you'd rather do yourself | a decision so central you want your own model's judgment, not a delegate's |

Roles already default sensibly (`architect`/`integrator`/`judge` → `deep`, `builder`/`verifier` →
`standard`, `scout`/`scribe` → `fast`), so override per task only when a specific task is harder
or easier than its role's norm — `[builder deep]` for one gnarly module, `[scribe fast]` to keep
docs cheap even under a `standard`-leaning plan.

## 8. Worked example

"Build a REST API for a todo app with auth, tests and docs" — 10 tasks, 4 waves, passes
`hive.py validate` with zero errors (`package.json` and the app entry are owned only by the
integrator, T6):

```markdown
goal: REST API for a todo app with auth, tests and docs
budget: balanced
agents: general
accept: npm test

## T1 [architect] Shared contracts
writes: src/types.ts, src/db/schema.sql, docs/api-contract.md
Define the Todo and User types, the auth token shape, and the SQL schema (users, todos
tables). Document every route's request/response shape in docs/api-contract.md: POST /auth/signup,
POST /auth/login, GET /todos, POST /todos, PATCH /todos/:id, DELETE /todos/:id. Passwords are
bcrypt-hashed; tokens are signed JWTs with a 24h expiry. Do not write any implementation code.

## T2 [builder] Auth module
writes: src/auth/
reads: src/types.ts, docs/api-contract.md
deps: T1
accept: node -e "require('./src/auth/index.js')"
Implement signup and login against the contract: POST /auth/signup hashes the password with
bcrypt and stores the user; POST /auth/login verifies the password and returns a signed JWT.
Export a `requireAuth` Express middleware that rejects requests with no or invalid bearer token
with 401. Do not touch src/index.ts or package.json.

## T3 [builder] Todos module
writes: src/todos/
reads: src/types.ts, docs/api-contract.md
deps: T1
accept: node -e "require('./src/todos/index.js')"
Implement the four todo routes against the contract, scoped to the authenticated user via
req.user.id (assume `requireAuth` has already run). Return 404 for a todo id that doesn't belong
to the caller. Do not touch src/index.ts or package.json.

## T4 [builder] Users module
writes: src/users/
reads: src/types.ts
deps: T1
accept: node -e "require('./src/users/index.js')"
Implement GET /users/me, returning the authenticated user's id and email (never the password
hash). Do not touch src/index.ts or package.json.

## T5 [scribe fast] Seed fixtures
writes: src/db/seed.ts
reads: src/db/schema.sql
deps: T1
accept: node -e "require('./src/db/seed.js')"
Write a seed script that inserts two demo users and three demo todos matching the schema, for
local dev and tests. No implementation logic beyond inserts.

## T6 [integrator] Wire the app
writes: package.json, src/index.ts, src/router.ts
reads: docs/api-contract.md
deps: T2, T3, T4
accept: node -e "require('./src/router.js')"
Create package.json (express, jsonwebtoken, bcrypt as dependencies; a "test" script running
the test suite). Build src/router.ts mounting src/auth, src/todos and src/users on their
contract paths, with requireAuth applied to /todos and /users routes. src/index.ts starts the
server on process.env.PORT or 3000.

## T7 [verifier standard] Auth and todos tests
writes: tests/auth.test.ts, tests/todos.test.ts
reads: docs/api-contract.md
deps: T2, T3
accept: npx vitest run tests/auth.test.ts tests/todos.test.ts
Write tests against src/auth and src/todos directly (not through the HTTP layer): signup then
login succeeds, login with a wrong password fails, an unauthenticated todo request is rejected,
a todo created by one user is invisible to another.

## T8 [scribe fast] Docs
writes: docs/README.md
reads: docs/api-contract.md
deps: T2, T3, T4
Write docs/README.md: setup steps, environment variables, and a curl example for each route in
the contract, including an auth header example.

## T9 [verifier] Integration tests
writes: tests/integration.test.ts
reads: docs/api-contract.md
deps: T6
accept: npx vitest run tests/integration.test.ts
Start the app from src/index.ts against an in-memory or temp SQLite db and drive it over HTTP
with supertest: signup, login, create a todo, list it, delete it, confirm 401 without a token.

## T10 [verifier] Final acceptance pass
writes: tests/e2e.test.ts
deps: T6, T7
accept: npm test
Add one end-to-end smoke test covering the full user journey (signup -> login -> create 3 todos
-> patch one done -> delete one -> list shows 2). Run the full suite and fix failures only inside
tests/e2e.test.ts; anything outside it goes through `ask`.
```

Waves: T1 alone (0); T2/T3/T4/T5 in parallel (1, all depend only on T1); T6/T7/T8 in parallel (2,
depending on the wave-1 builders); T9/T10 in parallel (3, both depend only on wave-2 tasks). No
two concurrent tasks share a `writes:` path.

## 9. Anti-patterns

- **Two tasks editing a barrel or index file.** `src/index.ts` or an `index.ts` re-export barrel
  is a shared file; give it to one integrator, not to "whichever builder finishes near it."
- **Specs that say "see above" or "same as T3."** The worker never sees T3's spec. Repeat the two
  or three relevant sentences, or move the shared part into `context.md`.
- **An integrator with no `deps:`.** If the integrator doesn't depend on the tasks whose output it
  wires together, `dispatch` can run it before they're done and it'll import files that don't
  exist yet.
- **Replicas everywhere.** `xN` multiplies cost by `N+1` for that task. Reserve it for the one or
  two pieces where being wrong is genuinely expensive (a tricky algorithm, a security-sensitive
  module) — not for routine CRUD or docs.
- **Planning 60 one-file tasks.** Fixed per-worker overhead (~15k tokens) dominates; merge related
  one-file tasks into 3–6-file module tasks instead, per §4.
