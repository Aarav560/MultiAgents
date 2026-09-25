# Project context
<!-- Every worker reads this file. -->

## Goal
Build a multi-tenant SaaS API: auth, organizations, users, projects, billing webhooks and
per-org rate limiting.

## Stack and conventions
- Node 20, TypeScript (strict), Fastify 4 for HTTP, Zod for validation, Vitest for tests.
- No database server: persistence is in-memory `Map`-based repositories behind a small
  interface in `src/db/client.ts` — a stand-in for a real DB, dependency-free by design.
- No JWT/webhook-signing libraries: `src/utils/jwt.ts` implements minimal HS256 JWT
  (header.payload.signature, HMAC-SHA256 via `node:crypto`) and Stripe-style webhook
  signatures are verified the same way (HMAC-SHA256 over `timestamp.body`).
- Every route file is a Fastify plugin: `export default async function routes(app: FastifyInstance)`.
  Route handlers validate `body`/`params`/`querystring` with the Zod schemas from `src/contracts/`
  (`schema.parse(...)`, never trust unparsed input) and call into a sibling `service.ts` that holds
  the actual logic — routes stay thin (parse, call service, map result to HTTP).
- Multi-tenancy: every org-scoped route is nested under `/orgs/:orgId/...`. `src/middleware/tenant.ts`
  is a Fastify `onRequest` hook that loads the org from `params.orgId`, checks the authenticated
  user is a member (via `request.user`, set by the auth middleware), and attaches `request.org`.
  Reject with 403 `ORG_FORBIDDEN` if the user is not a member.
- Auth: `src/middleware/auth.ts` is an `onRequest` hook that reads `Authorization: Bearer <jwt>`,
  verifies it with `verifyJwt` from `src/utils/jwt.ts`, and sets `request.user = { id, email }`.
  Missing/invalid token -> 401 `{ error: { code: "UNAUTHORIZED", message } }`.
- Error format (all error responses, set by `src/middleware/errorHandler.ts`, Fastify's
  `setErrorHandler`): `{ "error": { "code": "SNAKE_CASE", "message": "human text" } }`.
  Zod validation failures -> 400 `VALIDATION_ERROR` with the first Zod issue's message.
- Passwords: `src/utils/hash.ts` uses `node:crypto` `scrypt` (async, with a random salt stored
  as `salt:hash` hex, both same length) — never a third-party bcrypt package.
- Naming: camelCase for variables/functions, PascalCase for types, files kebab/camelCase matching
  existing names below. Zod schemas end in `Schema` (e.g. `CreateOrgSchema`); inferred types drop
  the suffix and use the entity name (e.g. `type CreateOrgInput = z.infer<typeof CreateOrgSchema>`).
- IDs: `crypto.randomUUID()`. Timestamps: ISO strings (`new Date().toISOString()`).
- Test command (whole suite): `npx vitest run`. Per domain: `npx vitest run tests/<domain>`.
  Tests build the Fastify app with `buildApp()` from `src/app.ts` (no network listen) and use
  `app.inject({ method, url, headers, payload })`.

## Layout
```
package.json               tsconfig.json               vitest.config.ts
.env.example                .gitignore                  README.md
src/
  app.ts                    server.ts                   config.ts
  types/
    domain.ts               http.ts
  contracts/
    auth.ts   orgs.ts   users.ts   projects.ts   billing.ts
  db/
    client.ts               schema.ts
  middleware/
    auth.ts   tenant.ts   rateLimit.ts   errorHandler.ts
  modules/
    auth/      routes.ts  service.ts
    orgs/      routes.ts  service.ts
    users/     routes.ts  service.ts
    projects/  routes.ts  service.ts
    billing/   routes.ts  service.ts  webhooks.ts
  utils/
    logger.ts               hash.ts                      jwt.ts
tests/
  auth/auth.test.ts         orgs/orgs.test.ts             users/users.test.ts
  projects/projects.test.ts billing/billing.test.ts       rateLimit/rateLimit.test.ts
  fixtures/index.ts
docs/
  API.md
```

## Contracts
Written by the architect task into `src/types/*.ts` and `src/contracts/*.ts`; everything else
codes against them by `reads:`. Expected shape (architect may refine names, but keep these):
- `src/types/domain.ts`: entity types — `User`, `Org`, `Membership` (`role: "owner"|"admin"|"member"`),
  `Project`, `Subscription`, `RateLimitBucket`.
- `src/types/http.ts`: `AppError`, `ErrorBody`, an `AuthedRequest` shape adding `user`/`org`.
- `src/contracts/auth.ts`: `SignupSchema`, `LoginSchema`, `RefreshSchema`, plus `AuthTokens`
  (`{ accessToken, refreshToken }`) and `AuthUser` (safe user view, no password hash).
- `src/contracts/orgs.ts`: `CreateOrgSchema`, `UpdateOrgSchema`, `AddMemberSchema`.
- `src/contracts/users.ts`: `UpdateUserSchema`.
- `src/contracts/projects.ts`: `CreateProjectSchema`, `UpdateProjectSchema`.
- `src/contracts/billing.ts`: `StripeWebhookEventSchema` (`{ id, type, data }`), `Subscription` view.
- `src/db/client.ts` (own task, reads contracts): exports `createStore()` returning typed
  in-memory repositories (`users`, `orgs`, `memberships`, `projects`, `subscriptions`) with
  `get/list/create/update/delete` methods keyed by id; `src/db/schema.ts` defines the row shapes.

## API surface (route <-> module)
- `POST /auth/signup`, `POST /auth/login`, `POST /auth/refresh` — `modules/auth`.
- `GET/PATCH /orgs/:orgId`, `POST /orgs`, `GET/POST /orgs/:orgId/members` — `modules/orgs`.
- `GET/PATCH /users/me` — `modules/users`.
- `GET/POST /orgs/:orgId/projects`, `GET/PATCH/DELETE /orgs/:orgId/projects/:projectId` — `modules/projects`.
- `POST /billing/webhooks/stripe`, `GET /orgs/:orgId/billing/subscription` — `modules/billing`.
- Rate limiting applies globally per org (or per IP pre-auth) via `middleware/rateLimit.ts`,
  registered as a Fastify `onRequest` hook in `app.ts`; exceeding the limit -> 429
  `{ error: { code: "RATE_LIMITED", message } }` with a `Retry-After` header.

## Constraints
- No new dependencies beyond `fastify`, `zod`, `typescript`, `vitest`, `tsx`, `@types/node`.
- Every file in the layout above is owned by exactly one plan task; do not create files outside it.
