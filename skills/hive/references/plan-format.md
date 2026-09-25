# Plan Format Reference

The hive orchestrator reads either `.hive/plan.md` (compact, recommended) or `.hive/plan.json` (schema below). If both exist, `plan.md` wins. The format is validated by `parse_plan_md`, `normalize_task`, `expand_replicas` and the `Plan` class in `hive.py`.

## plan.md Format

### Settings (before first task header)

Top-level keys appear before the first `## TaskID` header. Keys are case-insensitive and colon-delimited (one per line).

```
goal: <one sentence describing what the plan delivers>
budget: <lean | balanced | max>
agents: <general | hive | plugin | custom-prefix>
max_parallel: <integer, default from budget>
agent_overhead_tokens: <integer, default 14000>
accept: <shell command to verify overall success>
```

- **`goal`**: required but can be empty. Shown in briefs and status output.
- **`budget`**: maps tiers to models and sets default `max_parallel`:
  - `lean`: fast=haiku, standard=sonnet, deep=sonnet; max_parallel=4
  - `balanced` (default): fast=haiku, standard=sonnet, deep=opus; max_parallel=8
  - `max`: fast=sonnet, standard=opus, deep=opus; max_parallel=12
- **`agents`**: style for subagent type strings (one per task):
  - `general` → `general-purpose`
  - `hive` → `hive-<role>` (e.g., `hive-architect`)
  - `plugin` → `hivemind:hive-<role>`
  - `<custom-prefix>` → `<prefix><role>` (e.g., `custom-architect`)
- **`max_parallel`**: cap on concurrently running tasks. Overrides budget default.
- **`agent_overhead_tokens`**: per-task fixed overhead for tooling, briefs and communication. Tune this when estimating token usage.
- **`accept`**: repeatable. Each is a shell command passed to `hive verify --run`. Runs after all tasks done.

### Task Header

```
## ID [tokens] Title
```

**ID**: Letters, digits, underscore, period, hyphen only (matches `^[A-Za-z0-9_][A-Za-z0-9_.\-]*$`). Must be unique.

**[tokens]**: Optional space or comma-separated list. Parsed in this order:
- Role names: `architect`, `builder`, `scout`, `scribe`, `verifier`, `integrator`, `judge` (custom roles allowed; trigger warnings)
- Tier names: `fast`, `standard`, `deep`, `inherit` (default tier per role)
- Model names: `haiku`, `sonnet`, `opus`, `inherit` (overrides tier → model mapping)
- Replica count: `x2`, `x3`, etc. Creates k independent candidates plus a judge task that keeps the original ID. Candidates write to `.hive/candidates/ID/r1/`, `.hive/candidates/ID/r2/`, etc., mirroring the real owned paths.

Example: `## T1 [architect] Design contracts` sets role=architect (tier defaults to deep).
Example: `## T2 [builder, opus] Build module A` sets role=builder and forces opus model.
Example: `## T3 [x3] Three attempts` creates three candidates plus a judge; judge inherits T3's id, deps, and accept checks.

**Title**: Free text, shown in briefs and status. Extracted from first line of spec if omitted.

### Task Keys

Within a task block (between `## ID` headers), the following keys are recognized. Keys are case-insensitive. Lines not matching any key become part of the spec.

**List keys** (comma or whitespace separated, strip leading `./` and trailing `/**` from paths):
- `writes:` (paths this task owns exclusively; can end with `/` for directory ownership)
- `reads:` (paths to read before working; does not grant write access)
- `deps:` (task IDs this task depends on; must be done before starting)

**Accept checks**:
- `accept:` (shell command; repeatable. Runs before finishing. If any fail, task is not marked done.)

**Metadata** (only recognized if value is valid for that key; otherwise treated as spec prose):
- `role:` (role name)
- `tier:` (tier name)
- `model:` (model name)
- `judge_tier:` (for replica judges; overrides tier for judge role)
- `variants:` (pipe-separated hints for replica candidates; cycles through if fewer than replicas)

**Spec**: Any line not matching a key becomes part of the task spec. Code fences (``` lines) toggle fence mode; metadata inside code blocks is not parsed as keys, it is spec.

Example:
```
## T1 [architect, deep] Design API

writes: src/api.ts
spec:
Define the shape of GET /users, POST /users, and the error responses.
List the interfaces for Request and Response types.
```

## plan.json Format

JSON alternative to plan.md. Load if no plan.md exists.

```json
{
  "goal": "one sentence",
  "budget": "balanced",
  "agents": "general",
  "max_parallel": 8,
  "agent_overhead_tokens": 14000,
  "models": {
    "fast": "haiku",
    "standard": "sonnet",
    "deep": "opus"
  },
  "accept": [
    "command1",
    "command2"
  ],
  "tasks": [
    {
      "id": "T1",
      "title": "Design contracts",
      "role": "architect",
      "tier": "deep",
      "model": "opus",
      "replicas": 1,
      "writes": ["src/api.ts"],
      "reads": ["README.md"],
      "deps": [],
      "accept": ["npm test"],
      "spec": "Define...",
      "variants": ["approach A", "approach B"],
      "judge_tier": "deep"
    }
  ]
}
```

Top-level keys:
- `goal`, `budget`, `agents`, `max_parallel`, `agent_overhead_tokens`, `accept`: same as plan.md settings.
- `models`: map tier → model (only in JSON; plan.md merges into budget defaults via lines above `## T1`).
- `tasks`: array of task objects.

Task object keys:
- `id`, `title`, `spec`: as per plan.md headers and body.
- `role`, `tier`, `model`, `judge_tier`: as per plan.md metadata. All lowercase.
- `replicas`: integer >= 1; default 1.
- `writes`, `reads`, `deps`: arrays of strings. Paths in writes/reads are normalized; deps are task IDs.
- `accept`: array of shell commands (or single string; code normalizes both to array).
- `variants`: array of strings (hints to replica candidates).

## Path Normalization

Paths in `writes:` and `reads:` are normalized by stripping leading `./`, replacing backslashes with `/`, and removing trailing `/**`:
- `./src/foo.ts` → `src/foo.ts`
- `src/types/**` → `src/types/`
- `src/types/` → `src/types/` (directory ownership marker)

A path ending with `/` means the task owns the entire directory and everything in it.

## Role Default Tiers

When a task lists a role but no tier, the tier defaults to:
- `architect`, `integrator`, `judge`: `deep`
- `builder`, `verifier`: `standard`
- `scout`, `scribe`: `fast`

## Model Assignment

Each task is assigned a model by:
1. If `model:` key is set, use that.
2. Else if `tier:` is `inherit`, use `inherit` (the session's model).
3. Else map `tier` to model via the budget table (or overrides in plan.md `models:` lines or JSON `models` object).

`inherit` skips Claude model selection, letting the session's model handle the task.

## Replica Expansion

When a task has `replicas: k` (or `xk` in plan.md header):
1. The task spawns k candidate tasks with IDs `ID.r1`, `ID.r2`, ..., `ID.rk`.
2. Each candidate writes to `.hive/candidates/ID/r<i>/` (mirroring real owned paths).
3. A judge task inherits the original ID, role=judge, tier=deep (or `judge_tier` override).
4. The judge's `reads:` include `.hive/candidates/ID/`, and its `deps:` are the k replica IDs.
5. The judge's spec tells it to pick the best or merge strongest parts, then write final files.
6. If `variants:` list is provided, each candidate gets a hint from the list (cycling if fewer hints than replicas).

Example: Task T3 with `replicas: 2` and `variants: "prompt engineering | code search"` creates:
- T3.r1 (candidate 1, hint: prompt engineering)
- T3.r2 (candidate 2, hint: code search)
- T3 (judge; deps=[T3.r1, T3.r2]; inherits original writes/accept)

## Validation

`hive validate` checks:
- All task IDs are unique and match the charset.
- All specs are non-empty.
- All tiers and models (if set) are valid.
- All deps reference existing tasks and no cycles exist.
- No two tasks own overlapping paths unless one is an ancestor of the other in the dependency graph.
- Context.md exists and is under ~8000 chars (every worker pays for it).

## ID Charset

Task IDs match `^[A-Za-z0-9_][A-Za-z0-9_.\-]*$`: must start with alphanumeric or underscore, then alphanumeric, underscore, period, or hyphen. Common patterns: T1, stage1, feat_x, v2.3.
