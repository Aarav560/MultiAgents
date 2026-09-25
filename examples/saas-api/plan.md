goal: Build a multi-tenant SaaS API (auth, orgs, users, projects, billing webhooks, rate limiting) in Fastify + Zod + Vitest
budget: balanced
agents: general
accept: npx tsc --noEmit
accept: npx vitest run

## T1 [architect] Shared types and contracts
writes: src/types/domain.ts, src/types/http.ts, src/contracts/auth.ts, src/contracts/orgs.ts, src/contracts/users.ts, src/contracts/projects.ts, src/contracts/billing.ts
reads: README.md
Design the domain types and Zod contracts every other task codes against; follow the
"Contracts" and "API surface" sections of context.md exactly (same file names, same exported
names). `src/types/domain.ts`: `User` (id, email, passwordHash, createdAt), `Org` (id, name,
ownerId, createdAt), `Membership` (id, orgId, userId, role: "owner"|"admin"|"member"),
`Project` (id, orgId, name, description, status: "active"|"archived", createdAt), `Subscription`
(id, orgId, plan: "free"|"pro"|"enterprise", status: "active"|"past_due"|"canceled"),
`RateLimitBucket` (key, tokens, updatedAt). `src/types/http.ts`: `AppError` (a class extending
Error with `code: string` and `status: number`), `ErrorBody` (`{ error: { code, message } }`),
and an `AuthedRequest`-like interface documenting the `user`/`org` fields middleware attaches
(do not import fastify here; keep it a plain type others intersect with `FastifyRequest`).
`src/contracts/auth.ts`: `SignupSchema` (email, password min 8), `LoginSchema` (email, password),
`RefreshSchema` (refreshToken), `AuthTokens` type, `AuthUser` type (id, email, createdAt — no
password hash). `src/contracts/orgs.ts`: `CreateOrgSchema` (name), `UpdateOrgSchema` (name
optional), `AddMemberSchema` (userId, role). `src/contracts/users.ts`: `UpdateUserSchema` (email
optional). `src/contracts/projects.ts`: `CreateProjectSchema` (name, description optional),
`UpdateProjectSchema` (name, description, status all optional). `src/contracts/billing.ts`:
`StripeWebhookEventSchema` (id, type, data: record), and a `SubscriptionView` type. Export every
Zod schema and its inferred `z.infer` type with the naming convention from context.md. No logic,
only types and schemas — do not write any route, service, or db code.

## T2 [builder] Auth module: signup, login, refresh, JWT, password hashing
writes: src/modules/auth/routes.ts, src/modules/auth/service.ts, src/utils/jwt.ts, src/utils/hash.ts, tests/auth/auth.test.ts
reads: src/types/domain.ts, src/contracts/auth.ts, src/types/http.ts
deps: T1
accept: npx vitest run tests/auth
`src/utils/hash.ts`: `hashPassword(plain): Promise<string>` (node:crypto scrypt, random 16-byte
salt, returns `saltHex:hashHex`) and `verifyPassword(plain, stored): Promise<boolean>` using
`timingSafeEqual`. `src/utils/jwt.ts`: `signJwt(payload, secret, expiresInSeconds): string` and
`verifyJwt(token, secret): payload | null`, hand-rolled HS256 (base64url header.payload, HMAC-SHA256
signature over `header.payload`, base64url encoded, constant-time compare) — no external JWT
library. `src/modules/auth/service.ts`: in-memory user store access via the `Store` from
`src/db/client.ts` (import the type only from `src/types/domain.ts`; construct/receive the store
via a factory parameter so tests can inject one — do not import `src/db/client.ts` directly since
it is owned by another task; define a minimal local `UserRepo` interface in this file with
`get/create/findByEmail` and accept it as a constructor/function argument). Implement `signup`,
`login`, `refresh` returning `AuthTokens`; access tokens expire in 900s, refresh tokens in 30 days,
both signed with an `AUTH_SECRET` string passed in. `src/modules/auth/routes.ts`: Fastify plugin
registering `POST /auth/signup`, `POST /auth/login`, `POST /auth/refresh`, parsing bodies with the
T1 schemas, calling the service (constructed with an in-memory `Map`-backed `UserRepo` for now),
returning 201/200 with `AuthTokens` and mapping known errors (duplicate email, bad credentials) to
409/401 via `AppError`. `tests/auth/auth.test.ts`: build a minimal Fastify instance registering
just this plugin, exercise signup -> login -> refresh, and one duplicate-signup 409 case, and one
bad-password 401 case.

## T3 [builder] Orgs and users modules: membership, org CRUD, profile
writes: src/modules/orgs/routes.ts, src/modules/orgs/service.ts, src/modules/users/routes.ts, src/modules/users/service.ts, tests/orgs/orgs.test.ts, tests/users/users.test.ts
reads: src/types/domain.ts, src/contracts/orgs.ts, src/contracts/users.ts, src/types/http.ts
deps: T1
accept: npx vitest run tests/orgs tests/users
Both services take repo dependencies as constructor/function arguments (define local minimal
repo interfaces here, same pattern as T2 — do not import `src/db/client.ts`). `orgs/service.ts`:
`createOrg(userId, input)` creates the org and an owner `Membership` in one call; `getOrg`,
`updateOrg` (only owner/admin may rename — throw `AppError("ORG_FORBIDDEN", 403)` otherwise),
`listMembers`, `addMember` (only owner/admin may add; default role "member"). `orgs/routes.ts`:
`POST /orgs`, `GET /orgs/:orgId`, `PATCH /orgs/:orgId`, `GET /orgs/:orgId/members`,
`POST /orgs/:orgId/members`, all requiring an authenticated `request.user` (read it directly off
the request for this test harness; assume a decorator named `user` may be absent and treat a
missing one as 401). `users/service.ts`: `getSelf(userId)`, `updateSelf(userId, input)`.
`users/routes.ts`: `GET /users/me`, `PATCH /users/me`. Tests build a standalone Fastify instance
per test file registering only the relevant plugin(s) with an in-memory repo and a fake
authentication decorator (a `preHandler` that sets `request.user = { id: "u1", email: "a@a.com" }`)
so these tests do not depend on T2's real auth flow; cover create-org + add-member + forbidden-role
paths for orgs, and get/update for users.

## T4 [builder] Projects module
writes: src/modules/projects/routes.ts, src/modules/projects/service.ts, tests/projects/projects.test.ts
reads: src/types/domain.ts, src/contracts/projects.ts, src/types/http.ts
deps: T1
accept: npx vitest run tests/projects
`service.ts`: `listProjects(orgId)`, `createProject(orgId, input)`, `getProject(orgId, projectId)`
(throw `AppError("PROJECT_NOT_FOUND", 404)` if missing or wrong org), `updateProject`,
`deleteProject`, all against an injected minimal repo interface (same pattern as T2/T3 — define it
locally, do not import `src/db/client.ts`). `routes.ts`: `GET/POST /orgs/:orgId/projects`,
`GET/PATCH/DELETE /orgs/:orgId/projects/:projectId`, parsing bodies with the T1 project schemas.
Test: standalone Fastify instance with an in-memory repo and a fake `request.org = { id: "o1" }`
preHandler, covering create, list, get-404, update, delete.

## T5 [builder] Billing module and Stripe-style webhooks
writes: src/modules/billing/routes.ts, src/modules/billing/service.ts, src/modules/billing/webhooks.ts, tests/billing/billing.test.ts
reads: src/types/domain.ts, src/contracts/billing.ts, src/types/http.ts
deps: T1
accept: npx vitest run tests/billing
`webhooks.ts`: `verifyStripeSignature(rawBody: string, signatureHeader: string, secret: string,
toleranceSeconds = 300): boolean` — header format `t=<unix>,v1=<hex hmac>`; recompute HMAC-SHA256
of `${t}.${rawBody}` with `secret` via `node:crypto`, constant-time compare, reject if `t` is
outside tolerance. `service.ts`: `applyWebhookEvent(event)` updates/creates a `Subscription` for
the org referenced in `event.data` (accept `event.data.orgId`, `event.data.plan`,
`event.data.status`) against an injected repo interface (local minimal interface, same pattern as
T2-T4); `getSubscription(orgId)`. `routes.ts`: `POST /billing/webhooks/stripe` (reads the raw
body — configure the route with `config: { rawBody: true }` is not available by default in
Fastify, so instead re-stringify `request.body` with `JSON.stringify` for signature verification
in tests, and note in a code comment that a production build would use a raw-body plugin; verify
the `stripe-signature` header, parse+validate with `StripeWebhookEventSchema`, 400 on bad
signature or shape), `GET /orgs/:orgId/billing/subscription`. Test: standalone Fastify instance,
valid-signature webhook applies a subscription, invalid-signature webhook -> 400, and the GET
returns the applied subscription.

## T6 [builder x3] Per-org rate limiting middleware
writes: src/middleware/rateLimit.ts, tests/rateLimit/rateLimit.test.ts
reads: src/types/domain.ts, src/types/http.ts
deps: T1
accept: npx vitest run tests/rateLimit
variants: token bucket keyed by org id (refill per second) | sliding-window log keyed by org id (timestamps pruned per request) | fixed window counter keyed by org id (reset each window boundary)
Export `createRateLimiter(opts: { limit: number; windowMs: number })` returning a Fastify
`onRequest` hook `(request, reply) => void|Promise<void>` that keys on `request.org?.id ??
request.ip`, tracks state in an internal `Map` (no external store), and on exceeding `limit`
requests per `windowMs` sets a `Retry-After` header (seconds) and throws an `AppError`
(`"RATE_LIMITED"`, 429) — do not call `reply.send` directly, let `errorHandler` format it. Also
export `resetRateLimiter()` for tests to clear state between cases. Test: build a bare Fastify
instance with one dummy route guarded by the hook (`limit: 3, windowMs: 1000`), assert the 4th
request in the same window is 429 with a `Retry-After` header, and that after resetting the
limiter or waiting past the window a further request succeeds (use a short windowMs and
`await new Promise(r => setTimeout(r, windowMs + 20))`, not fake timers).

## T7 [builder] In-memory data store
writes: src/db/client.ts, src/db/schema.ts
reads: src/types/domain.ts
deps: T1
`schema.ts`: row types for each table, structurally equal to the T1 domain types plus any
storage-only fields (none expected — re-export or narrow from `src/types/domain.ts`).
`client.ts`: `createStore()` returns an object with five `Repo<T>`-shaped members — `users`,
`orgs`, `memberships`, `projects`, `subscriptions` — each backed by its own `Map<string, T>` and
exposing `get(id)`, `list(filter?: Partial<T>)` (simple equality filter over given fields),
`create(data)` (assigns `id` via `crypto.randomUUID()` if absent, `createdAt` if the type has
one), `update(id, patch)`, `delete(id)`. Also export a `Repo<T>` interface with that shape and a
`findByEmail(email)` convenience method specifically on `users`. This is the real store the
integrator wires into each module in T12; it does not need to satisfy the local per-module repo
interfaces byte-for-byte, only structurally (same method names/signatures).

## T8 [builder] Auth, tenant and error-handling middleware
writes: src/middleware/auth.ts, src/middleware/tenant.ts, src/middleware/errorHandler.ts
reads: src/types/domain.ts, src/types/http.ts, src/utils/jwt.ts, src/db/client.ts
deps: T1, T2, T7
`errorHandler.ts`: exports `errorHandler(error, request, reply)` matching Fastify's
`setErrorHandler` signature — if `error` is an `AppError` (from `src/types/http.ts`), reply with
its `status` and `{ error: { code, message } }`; if it is a Zod error, reply 400
`{ error: { code: "VALIDATION_ERROR", message: <first issue message> } }`; otherwise log via
`src/utils/logger.ts` and reply 500 `{ error: { code: "INTERNAL", message: "internal error" } }`.
`auth.ts`: exports `createAuthHook(secret: string)` returning an `onRequest` hook that reads
`Authorization: Bearer <token>`, calls `verifyJwt` from `src/utils/jwt.ts`, sets
`(request as any).user = { id: payload.sub, email: payload.email }` on success, else throws
`AppError("UNAUTHORIZED", 401)`. `tenant.ts`: exports `createTenantHook(store: ReturnType<typeof
import("../db/client").createStore>)` returning an `onRequest` hook that reads `params.orgId`,
loads the org and the membership for `request.user.id`, throws `AppError("ORG_FORBIDDEN", 403)`
if no membership, else sets `(request as any).org = org`. Both hooks must run after body/params
are available (Fastify `onRequest` has params already). No tests owned here; these are exercised
through T12's integration and T13's verification.

## T9 [scribe fast] Config and shared utils
writes: src/config.ts, .env.example, src/utils/logger.ts
reads: src/types/domain.ts
deps: T1
`src/config.ts`: `loadConfig()` reads `process.env` with defaults — `PORT` (3000), `AUTH_SECRET`
(default `"dev-secret-change-me"`), `STRIPE_WEBHOOK_SECRET` (default `"whsec_dev"`),
`RATE_LIMIT_MAX` (100), `RATE_LIMIT_WINDOW_MS` (60000) — returns a typed `Config` object. `.env.example`:
one `KEY=example` line per config var above. `src/utils/logger.ts`: a tiny leveled logger
(`info/warn/error(msg: string, meta?: Record<string, unknown>)`) writing structured
`JSON.stringify({ level, msg, ...meta, ts })` lines to stdout/stderr — no external logging
dependency.

## T10 [scribe fast] Test fixtures
writes: tests/fixtures/index.ts
reads: src/types/domain.ts
deps: T1
Export small builder functions for tests across modules: `makeUser(overrides?)`,
`makeOrg(overrides?)`, `makeMembership(overrides?)`, `makeProject(overrides?)`,
`makeSubscription(overrides?)`, each returning a fully populated object of the matching T1 domain
type with sensible defaults (fresh `crypto.randomUUID()` id, current ISO timestamp) overridden by
the passed partial. Pure data, no Fastify or store imports.

## T11 [scribe fast] API docs
writes: docs/API.md, README.md
reads: src/contracts/auth.ts, src/contracts/orgs.ts, src/contracts/users.ts, src/contracts/projects.ts, src/contracts/billing.ts
deps: T1, T2, T3, T4, T5, T6
`docs/API.md`: one section per module (Auth, Orgs, Users, Projects, Billing) listing each route
from context.md's "API surface" with method, path, request body shape (from the Zod contracts),
success response, and the error codes it can return. `README.md`: what this demo is, how to
install (`npm install`), run (`npm run dev`), test (`npx vitest run`), and typecheck
(`npx tsc --noEmit`); link to `docs/API.md`.

## T12 [integrator deep] Wire the app together
writes: package.json, tsconfig.json, vitest.config.ts, .gitignore, src/app.ts, src/server.ts
reads: src/db/client.ts, src/middleware/auth.ts, src/middleware/tenant.ts, src/middleware/rateLimit.ts, src/middleware/errorHandler.ts, src/modules/auth/routes.ts, src/modules/orgs/routes.ts, src/modules/users/routes.ts, src/modules/projects/routes.ts, src/modules/billing/routes.ts, src/config.ts
deps: T2, T3, T4, T5, T6, T7, T8, T9
accept: npx tsc --noEmit
accept: npx vitest run
`package.json`: name `saas-api-example`, `type: module`, scripts `dev` (`tsx watch
src/server.ts`), `build` (`tsc -p tsconfig.json`), `test` (`vitest run`), `typecheck` (`tsc
--noEmit`); dependencies `fastify`, `zod`; devDependencies `typescript`, `vitest`, `tsx`,
`@types/node`. `tsconfig.json`: strict mode, `target`/`module` `ES2022`, `moduleResolution`
`bundler`, `outDir dist`, includes `src` and `tests`. `vitest.config.ts`: default Vitest config
pointing at `tests/**/*.test.ts`. `.gitignore`: `node_modules/`, `dist/`, `.env`. `src/app.ts`:
`export function buildApp(overrides?: { store?: ReturnType<typeof createStore> }): FastifyInstance`
— builds a store (or uses the override, so tests can inject one), registers `errorHandler`,
registers the rate-limit hook globally with config from `loadConfig()`, registers each module's
routes plugin (wrapping org-scoped ones so the auth hook then the tenant hook run first via
`fastify.register(plugin, { prefix: ... })` combined with `addHook`, or by composing the hooks
directly inside each affected route's `preHandler` array — pick one approach and apply it
consistently), and returns the app un-started (no `.listen()`). `src/server.ts`: imports
`buildApp`, calls `.listen({ port: loadConfig().PORT })`, logs the bound address via
`src/utils/logger.ts`, and is the only file that starts the process (guarded so tests never invoke
it, e.g. no top-level side effects if `buildApp` is imported instead of running as `main`). Fix
any small type mismatches between modules by adapting call sites in `app.ts`, never by editing
files owned by other tasks — use `ask` if a contract genuinely needs to change.

## T13 [verifier] Full-suite check
deps: T12
accept: npx tsc --noEmit
accept: npx vitest run
Run the full type check and test suite exactly as an integrator would ship it. Own no files: if
something is broken, fix it only inside files you own (none), otherwise record the exact failure
with `ask` naming the file and task that must fix it, and fail this task with that note.
