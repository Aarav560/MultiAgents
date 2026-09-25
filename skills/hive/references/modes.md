# Modes: plan shapes, not switches

A "mode" is not a flag to `hive.py` — it's a shape you draw in `plan.md` with the same
primitives every plan uses: `writes:`, `reads:`, `deps:`, `accept:`, `variants:`, `xN`. Pick the
shape that matches how the work depends on itself, then dispatch as usual. Nothing below is
special-cased in the kernel; `validate`, `dispatch` and `verify` treat every plan the same way.

## Swarm: flat fan-out, no deps

Use for independent units that need nothing from each other: a test file per module, a config
per service, a translation per page. Everything lands in wave 0 and runs up to `max_parallel`
at a time.

```
T1  T2  T3  ...  T12        (one wave, no deps)
```

```markdown
goal: Add unit tests for every module in src/
budget: balanced

## T1 [builder fast] Tests for auth.py
writes: tests/test_auth.py
reads: src/auth.py
accept: pytest tests/test_auth.py
Cover login, logout and the expired-token path.

## T2 [builder fast] Tests for billing.py
writes: tests/test_billing.py
reads: src/billing.py
accept: pytest tests/test_billing.py
Cover charge, refund and the currency-mismatch error.
```
Repeat T3..T12 the same way — one task per module, each owning only its own test file so
`validate` never sees a write conflict. `fast` (haiku on `lean`/`balanced`) is enough for
mechanical test-writing; bump a module to `standard` only if its logic is genuinely tricky.

**Cost/speed:** cheapest, fastest shape. One wave, N workers, no idle waiting on a dependency.
Cost scales linearly with N; wall-clock is roughly one task's time, capped by how many fit
under `max_parallel` (a 12th task with `max_parallel 8` waits for a slot). Swarms rarely need
`deep`.

## Unite: fan-out/fan-in on one goal

Use when many workers build **one** system that has to fit together. An architect writes the
shared contract, builders fan out against it, an integrator fans the pieces into one whole, a
verifier checks the result end to end.

```
        ┌── T2 builder ──┐
T1 architect ──┤          ├── T4 integrator ── T5 verifier
        └── T3 builder ──┘
```

```markdown
goal: Add a checkout flow to the store API
budget: balanced

## T1 [architect] Checkout contracts
writes: src/types/checkout.ts
Define Cart, Order and PaymentResult types and the signatures builders code against.

## T2 [builder] Cart service
writes: src/checkout/cart.ts
reads: src/types/checkout.ts
deps: T1
accept: npx vitest run src/checkout/cart
Implement add/remove/total against the Cart type.

## T3 [builder] Payment and order services
writes: src/checkout/payment.ts, src/checkout/order.ts
reads: src/types/checkout.ts
deps: T1
accept: npx vitest run src/checkout/payment src/checkout/order
Implement charge() returning PaymentResult, and createOrder(cart, PaymentResult).

## T4 [integrator deep] Wire checkout together
writes: src/checkout/index.ts
reads: src/checkout/cart.ts, src/checkout/payment.ts, src/checkout/order.ts
deps: T2, T3
Export checkout(cart) calling cart -> payment -> order, reconciling any mismatches.

## T5 [verifier] End-to-end checkout test
writes: tests/checkout.e2e.ts
reads: src/checkout/index.ts
deps: T4
accept: npx vitest run tests/checkout.e2e
Exercise the full path including a declined payment.
```

**Cost/speed:** 4 waves (T1 → T2/T3 in parallel → T4 → T5). Wall-clock is the sum of those
waves, not of all tasks — put as many independent builders as possible in the fan-out wave.
Give the architect and integrator `deep`; builders can usually stay `standard`.

## Pipeline: staged waves

Use when later work genuinely needs earlier work's output — commonly scouts first, so an
architect plans against facts instead of guesses.

```
T1 scout          →  T2 architect  →  T3 builder  T4 builder  →  T5 integrator
(findings.md)          (contract)      (parallel)                (final wave)
```

```markdown
goal: Migrate the payments module from callbacks to async/await
budget: balanced

## T1 [scout fast] Map payments module
writes: .hive/findings/payments.md
reads: src/payments/
List every async operation in src/payments/gateway.js, every caller elsewhere in the repo,
and their callback signatures.

## T2 [architect deep] Async contract
writes: src/payments/types.ts
reads: .hive/findings/payments.md
deps: T1
Define Promise-based signatures replacing callbacks, covering every call site T1 found.

## T3 [builder] Convert gateway.js
writes: src/payments/gateway.js
reads: src/payments/types.ts
deps: T2
accept: npm test -- payments/gateway
Convert to async/await against the contract.

## T4 [builder] Convert callers
writes: src/orders.js, src/refunds.js
reads: src/payments/types.ts
deps: T2
accept: npm test -- orders refunds
Update both callers to await the new payments API.

## T5 [integrator deep] Final wiring and cleanup
writes: src/payments/index.js
reads: src/payments/gateway.js, src/orders.js, src/refunds.js
deps: T3, T4
accept: npm test
Remove the old callback shims; export the async API as the public surface.
```

Scouts are read-only by role instructions, so T1 is safe to run before anything else exists to
conflict with. `.hive/findings/` is a convention, not a kernel feature — any directory the
scout task owns works the same way.

**Cost/speed:** as many waves as stages, so wall-clock is the sum of one task per stage (more if
a stage fans out). Costs more than swarm/unite for the same file count because scouts add a wave
that does no building, but it pays for itself when skipping it would produce a wrong contract
builders then have to redo. Keep scouts `fast`.

## Consensus: xN replicas with a judge

Use when one task is worth getting right more than cheap: a tricky algorithm, a
security-sensitive piece, a design that's expensive to change later. `xN` expands a task into N
independent candidates plus a judge that keeps the task's original id, so nothing downstream
knows consensus happened.

```
T1 (writes: src/rank.ts, x3, variants: a | b | c)
        │  expands to
   T1.r1  T1.r2  T1.r3        (candidates, independent)
        └────┬────┘
         T1 (judge)            (picks/merges, writes the real path)
        │
       T2                       (depends on T1 as normal)
```

```markdown
goal: Design the ranking algorithm for search results
budget: balanced

## T1 [builder x3] Ranking algorithm
writes: src/rank.ts
variants: recency-weighted | popularity-weighted | hybrid tunable weights
judge_tier: deep
accept: npx vitest run src/rank
Implement rank(results, query) against the RankInput/RankOutput types in src/types.ts.
Optimize for relevance on tests/fixtures/queries.json.

## T2 [builder] Wire ranking into search
writes: src/search.ts
reads: src/rank.ts
deps: T1
accept: npx vitest run src/search
Call rank() from the search handler and return its output.
```

`expand_replicas` turns `T1` into `T1.r1`, `T1.r2`, `T1.r3`, each writing to
`.hive/candidates/T1/r1/src/rank.ts` (and r2, r3 — candidate paths mirror the real ones under
that prefix), plus a judge that keeps the id `T1`, reads `.hive/candidates/T1/` and the
original `reads:`, and writes the real path `src/rank.ts`. Each `variants:` entry hints one
candidate; extra candidates cycle through them. The judge's tier defaults to `deep`;
`judge_tier:` overrides it.

Downstream tasks just write `deps: T1` — they never see the replicas. `T2` cannot start until
the judge (`T1`) is done, which itself waits on all three candidates.

**Cost/speed:** an `xN` task costs N+1 workers instead of 1, and adds one full task's worth of
wall-clock versus a single-candidate task (nothing downstream starts until the judge finishes).
Reserve it for the few tasks where being wrong is expensive; running every task at x3 defeats
the token discipline the rest of the plan buys you.

## Recon: scouts only

Use when the codebase or problem is unfamiliar enough that you shouldn't commit to a real plan
yet. The whole plan is scout tasks; you read their findings, then write a second, real plan
(fresh `plan.md`, or `H reset --all` plus new tasks) once you know the shape of the work.

```
T1 scout   T2 scout      (one wave, all read-only)
      └───┬───┘
     .hive/findings/*.md  →  you read these  →  you write the real plan.md
```

```markdown
goal: Understand the legacy billing module before planning its rewrite
budget: lean

## T1 [scout] Map billing module structure and tests
writes: .hive/findings/billing-structure.md
reads: src/billing/, tests/billing/
List every file, its exports, which modules import from src/billing/, and which
behaviors have no test.

## T2 [scout] Map billing external dependencies
writes: .hive/findings/billing-deps.md
reads: src/billing/
List every third-party package and external API touched, with version pins.
```

There is nothing to `dispatch` after this wave except the plan you write next — recon plans
have no builders and usually no second wave. `H verify --run` on a recon plan just confirms the
findings files exist.

**Cost/speed:** cheap (all `fast`, one wave) and pure overhead against the real work — spend it
only when you'd otherwise be guessing at file layout, ownership boundaries or conventions well
enough to get `validate`'s write-conflict check and the specs wrong.

## Hybrid: mixing every shape in one plan

Real plans usually aren't one pure mode. A typical hybrid: recon to learn the codebase,
architect to set contracts from what recon found, a swarm of builders against those contracts,
one `xN` consensus task for the piece worth getting right, an integrator to wire it together,
a verifier at the end.

```
T1 scout                                (recon)
     │
   T2 architect                         (contracts, from findings)
     ┌─────┼───────────┐
   T3 builders(swarm) T4 builder x3     (fan-out + consensus)
     └─────┴───────────┘
              T5 integrator             (fan-in)
                   │
              T6 verifier
```

```markdown
goal: Rebuild the recommendations engine
budget: balanced

## T1 [scout fast] Map recommendations module and data sources
writes: .hive/findings/reco.md
reads: src/recommendations/, src/data/
List current files and entry points, plus schemas and query patterns for the tables
recommendations will read.

## T2 [architect deep] Recommendations contracts
writes: src/recommendations/types.ts
reads: .hive/findings/reco.md
deps: T1
Define RecoInput, RecoOutput and the per-strategy signature builders code against.

## T3 [builder fast] Popular-items and recently-viewed strategies
writes: src/recommendations/strategies/popular.ts, src/recommendations/strategies/recent.ts
reads: src/recommendations/types.ts
deps: T2
accept: npx vitest run recommendations/strategies
Implement both strategies against the contract.

## T4 [builder x3] Ranking/blend strategy
writes: src/recommendations/strategies/blend.ts
reads: src/recommendations/types.ts
deps: T2
variants: weighted average | learned linear model | rule-based fallback chain
accept: npx vitest run recommendations/strategies/blend
Blend the other strategies' output into one ranked list.

## T5 [integrator deep] Wire recommendation strategies
writes: src/recommendations/index.ts
reads: src/recommendations/strategies/popular.ts, src/recommendations/strategies/recent.ts, src/recommendations/strategies/blend.ts
deps: T3, T4
Export recommend(user) that runs all strategies and returns blend's output.

## T6 [verifier] End-to-end recommendations test
writes: tests/recommendations.e2e.ts
reads: src/recommendations/index.ts
deps: T5
accept: npx vitest run tests/recommendations.e2e
Exercise recommend() for a new user (no history) and a returning user.
```

**Cost/speed:** total cost is the sum of every task, including the `x3`'s extra 3 workers.
Wall-clock is the critical path T1 → T2 → (T3/T4, gated by T4's judge) → T5 → T6 — five waves
deep even though 9 tasks exist, since `dispatch` batches whatever is ready in each wave rather
than running tasks one at a time. Run `H estimate` before dispatching a plan with an `xN` or
several waves, so the shape you drew is the shape you meant to pay for.

## Streaming dispatch vs wave-by-wave dispatch

Both use the same `dispatch` command; the difference is how often you call it.

- **Wave-by-wave:** send everything `dispatch` prints, wait for the whole batch to report back,
  then call `dispatch` again. Simple to narrate, and fine for plans that are mostly one or two
  waves (swarm, most unite plans).
- **Streaming:** call `dispatch` again after **each** worker reports, not after the whole batch.
  Because `dispatch` computes readiness from `.hive/status/*.json` on every call, a task whose
  only dependency just finished starts immediately instead of waiting for its siblings in the
  same wave. This matters most in pipeline and hybrid plans with uneven task sizes — a `fast`
  scout that finishes in a minute shouldn't sit idle while a `deep` architect in the same wave
  is still three tasks from ready.

Use wave-by-wave when the plan's waves are similar in size and you're driving manually. Use
streaming whenever agents run in the background or task durations within a wave vary a lot —
it's what step 6 of the protocol in `SKILL.md` means by "if agents run in the background, run
it after each completion." Both are the same kernel call; nothing in `plan.md` changes between
them.
