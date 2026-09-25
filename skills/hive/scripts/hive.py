#!/usr/bin/env python3
"""hive - zero-dependency orchestration kernel for Claude Code multi-agent runs.

The orchestrator (main Claude session) writes a plan once. This script does the
bookkeeping deterministically so no model spends tokens on it:

  validate   DAG checks, cycle detection, file-ownership conflict detection
  dispatch   picks ready tasks (critical path first), marks them running and
             prints the tiny Agent-tool calls to fire in parallel
  brief      worker-side: prints one worker's full assignment from disk
  done/fail  worker-side: records the result (one file per task, no locks)
  status     compact progress view
  verify     checks owned files exist and runs acceptance commands

Workers read their own brief, so the orchestrator never re-sends context, and
each worker replies with one line, so the orchestrator's context stays small.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

VERSION = "1.0.0"


def _find_hive_dir() -> Path:
    env = os.environ.get("HIVE_DIR")
    if env:
        return Path(env)
    here = Path(__file__).resolve()
    # When running as the copy inside <project>/.hive/bin/, use that .hive.
    if here.parent.name == "bin" and (here.parent.parent / "context.md").exists():
        try:
            return here.parent.parent.relative_to(Path.cwd().resolve())
        except ValueError:
            return here.parent.parent
    return Path(".hive")


HIVE = _find_hive_dir()
ROOT = HIVE.parent  # project root: every plan path is relative to it
STATUS_DIR = HIVE / "status"
REQ_DIR = HIVE / "requests"
BOARD = HIVE / "board.md"
CONTEXT = HIVE / "context.md"
PLAN_MD = HIVE / "plan.md"
PLAN_JSON = HIVE / "plan.json"

# --------------------------------------------------------------------------
# Intelligence dial: tier -> model per budget. "inherit" = the session's model.
# --------------------------------------------------------------------------
BUDGETS = {
    "lean": {"models": {"fast": "haiku", "standard": "sonnet", "deep": "sonnet"}, "max_parallel": 4},
    "balanced": {"models": {"fast": "haiku", "standard": "sonnet", "deep": "opus"}, "max_parallel": 8},
    "max": {"models": {"fast": "sonnet", "standard": "opus", "deep": "opus"}, "max_parallel": 12},
}
TIERS = ("fast", "standard", "deep", "inherit")
MODELS = ("haiku", "sonnet", "opus", "inherit")
TIER_WEIGHT = {"fast": 1, "standard": 2, "deep": 3, "inherit": 3}

ROLES = {
    "architect": (
        "deep",
        "You design before others build. Produce precise, minimal contracts (types, interfaces, "
        "schemas, file layout) that parallel builders can code against without talking to each "
        "other. Prefer explicit names and signatures over prose. No placeholder TODOs.",
    ),
    "builder": (
        "standard",
        "You implement production-quality code for exactly the files you own. Code against the "
        "contracts and upstream results; match the project's conventions. Complete, working code: "
        "no stubs, no TODOs, no pseudo-code. Run the acceptance checks yourself before finishing.",
    ),
    "scout": (
        "fast",
        "You investigate and report; you do not change source code. Read only what you need, "
        "then write your findings to the file you own as dense bullet points: facts, file paths, "
        "line numbers, gotchas. No filler.",
    ),
    "scribe": (
        "fast",
        "You write docs, configs, fixtures, boilerplate and repetitive files quickly and "
        "accurately, following the contracts and conventions exactly.",
    ),
    "verifier": (
        "standard",
        "You test and harden. Run the acceptance commands, write the tests you own, and fix "
        "defects only inside files you own. Anything broken elsewhere goes through `ask`.",
    ),
    "integrator": (
        "deep",
        "You unite parallel work into one coherent whole: wire modules together, reconcile "
        "mismatches between upstream outputs, and make the combined system run end to end.",
    ),
    "judge": (
        "deep",
        "You receive several independent candidate solutions. Evaluate each against the spec and "
        "acceptance checks, pick the best or merge the strongest parts, and write the final "
        "version to the real paths you own. State the winner in your done note.",
    ),
}

ID_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.\-]*$")


def die(msg: str, code: int = 1) -> None:
    print(f"hive: {msg}", file=sys.stderr)
    sys.exit(code)


# Windows installs usually provide `python`, not `python3`.
PY = "python" if os.name == "nt" else "python3"


def hive_cmd() -> str:
    return f"{PY} {HIVE.as_posix()}/bin/hive.py"


def at_root(p: str) -> Path:
    return ROOT / p


def root_prefix() -> str:
    r = ROOT.as_posix()
    return "" if r in ("", ".") else r


def norm_path(p: str) -> str:
    p = p.strip().replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    if p.endswith("/**"):
        p = p[:-2]
    return p


def is_dir_spec(p: str) -> bool:
    return p.endswith("/")


def paths_overlap(a: str, b: str) -> bool:
    if a == b:
        return True
    if is_dir_spec(a) and b.startswith(a):
        return True
    if is_dir_spec(b) and a.startswith(b):
        return True
    return False


def split_list(v) -> list:
    if v is None:
        return []
    if isinstance(v, list):
        return [str(x).strip() for x in v if str(x).strip()]
    s = str(v).strip()
    if s in ("", "-", "none"):
        return []
    return [x.strip() for x in s.split(",") if x.strip()]


# --------------------------------------------------------------------------
# Plan loading: plan.md (compact, recommended) or plan.json
# --------------------------------------------------------------------------
HEADER_RE = re.compile(r"^##\s+([^\s\[]+)\s*(?:\[([^\]]*)\])?\s*(.*)$")
LIST_KEYS = {"writes", "reads", "deps"}


def parse_plan_md(text: str) -> dict:
    """Parse the compact markdown plan format (see references/plan-format.md)."""
    plan: dict = {"tasks": []}
    cur = None
    spec_lines: list = []
    in_fence = False

    def flush():
        if cur is not None:
            cur["spec"] = "\n".join(spec_lines).strip()
            plan["tasks"].append(cur)

    for raw in text.splitlines():
        line = raw.rstrip()
        if line.strip().startswith("```"):
            in_fence = not in_fence
        m = HEADER_RE.match(line) if not in_fence else None
        if m:
            flush()
            spec_lines = []
            cur = {"id": m.group(1), "title": m.group(3).strip()}
            for tok in (m.group(2) or "").replace(",", " ").split():
                t = tok.lower()
                if t in ROLES:
                    cur["role"] = t
                elif t in TIERS:
                    cur["tier"] = t
                elif t in MODELS:
                    cur["model"] = t
                elif re.fullmatch(r"x\d+", t):
                    cur["replicas"] = int(t[1:])
                else:
                    cur["role"] = t  # custom role; validate() will warn
            continue
        if cur is None:
            if in_fence or line.lstrip().startswith("#") or ":" not in line:
                continue
            k, _, v = line.partition(":")
            k, v = k.strip().lower(), v.strip()
            if k == "accept":
                plan.setdefault("accept", []).append(v)
            elif k in ("max_parallel", "agent_overhead_tokens", "pack"):
                try:
                    plan[k] = int(v)
                except ValueError:
                    die(f"plan.md: {k} must be an integer, got {v!r}")
            elif k:
                plan[k] = v
            continue
        km = re.match(r"^(writes|reads|deps|accept|variants|judge_tier|model|tier|role|foreach):\s*(.*)$", line.strip()) if not in_fence else None
        if km and km.group(1) in ("model", "tier", "judge_tier", "role"):
            allowed = {"model": MODELS, "tier": TIERS, "judge_tier": TIERS, "role": tuple(ROLES)}[km.group(1)]
            if km.group(2).strip().lower() not in allowed:
                km = None  # ordinary spec prose such as "model: the User record"
        if km:
            k, v = km.group(1), km.group(2)
            if k in LIST_KEYS:
                cur.setdefault(k, []).extend(split_list(v))
            elif k == "accept":
                if v:
                    cur.setdefault("accept", []).append(v)
            elif k == "variants":
                cur["variants"] = [x.strip() for x in v.split("|") if x.strip()]
            elif k == "foreach":
                cur["foreach"] = v
            else:
                cur[k] = v.lower()
        else:
            spec_lines.append(line)
    flush()
    return plan


def read_plan_raw() -> dict:
    if PLAN_MD.exists():
        return parse_plan_md(PLAN_MD.read_text(encoding="utf-8"))
    if PLAN_JSON.exists():
        try:
            return json.loads(PLAN_JSON.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            die(f"plan.json is not valid JSON (line {e.lineno}, col {e.colno}): {e.msg}")
    die(f"no plan found. Run `{hive_cmd()} init`, then write {PLAN_MD}")
    return {}


def normalize_task(t: dict) -> dict:
    t = dict(t)
    t["id"] = str(t.get("id", "")).strip()
    role = str(t.get("role") or "builder").lower()
    t["role"] = role
    default_tier = ROLES.get(role, ("standard", ""))[0]
    t["tier"] = str(t.get("tier") or default_tier).lower()
    if t.get("model"):
        t["model"] = str(t["model"]).lower()
    t["writes"] = [norm_path(p) for p in split_list(t.get("writes"))]
    t["reads"] = [norm_path(p) for p in split_list(t.get("reads"))]
    t["deps"] = list(dict.fromkeys(split_list(t.get("deps"))))
    acc = t.get("accept") or []
    t["accept"] = [acc] if isinstance(acc, str) else list(acc)
    t["spec"] = str(t.get("spec") or "").strip()
    t["title"] = (str(t.get("title") or "").strip() or t["spec"].split("\n")[0][:60]).strip()
    try:
        t["replicas"] = int(t.get("replicas") or 1)
    except (TypeError, ValueError):
        t["replicas"] = 1
    return t


FOREACH_VARS = ("item", "name", "stem", "dir", "slug", "i")


def foreach_items(spec) -> list:
    if isinstance(spec, list):
        return [str(x) for x in spec]
    spec = str(spec).strip()
    if spec.startswith("glob:"):
        pat = spec[5:].strip()
        return sorted(p.relative_to(ROOT).as_posix() for p in ROOT.glob(pat) if ".hive" not in p.parts)
    m = re.fullmatch(r"range:\s*(-?\d+)\s*\.\.\s*(-?\d+)", spec)
    if m:
        lo, hi = int(m.group(1)), int(m.group(2))
        return [str(n) for n in range(lo, hi + 1)]
    return [x.strip() for x in spec.split(",") if x.strip()]


def expand_foreach(tasks: list) -> list:
    """A task with `foreach:` becomes one task per item. Placeholders {item} {name}
    {stem} {dir} {slug} {i} are substituted everywhere (plain replace, so code
    braces in specs are safe). Deps on the template id fan in to every copy."""
    out, fan = [], {}
    for t in tasks:
        if not t.get("foreach"):
            out.append(t)
            continue
        items = foreach_items(t["foreach"])
        if not items:
            die(f"{t['id']}: foreach matched no items ({t['foreach']})")
        ids = []
        for n, item in enumerate(items, 1):
            base = item.rstrip("/").split("/")[-1]
            stem = base.rsplit(".", 1)[0] if "." in base else base
            v = {"item": item, "name": base, "stem": stem, "dir": item.rsplit("/", 1)[0] if "/" in item else ".",
                 "slug": re.sub(r"[^A-Za-z0-9_.-]+", "-", stem).strip("-") or str(n), "i": str(n)}

            def sub(x):
                if isinstance(x, list):
                    return [sub(y) for y in x]
                if isinstance(x, str):
                    for k in FOREACH_VARS:
                        x = x.replace("{" + k + "}", v[k])
                return x

            c = {k: sub(val) for k, val in t.items() if k != "foreach"}
            c["writes"] = [norm_path(w) for w in c["writes"]]
            c["reads"] = [norm_path(r) for r in c["reads"]]
            if c["id"] == t["id"]:
                c["id"] = f"{t['id']}-{v['slug']}"
            ids.append(c["id"])
            out.append(c)
        fan[t["id"]] = ids
    if fan:
        for t in out:
            deps = []
            for d in t["deps"]:
                deps += fan.get(d, [d])
            t["deps"] = list(dict.fromkeys(deps))
    return out


def expand_replicas(tasks: list) -> list:
    """A task with replicas=k becomes k independent candidates plus a judge that
    keeps the original id (so downstream deps are unchanged)."""
    out = []
    for t in tasks:
        k = t["replicas"]
        if k < 2:
            out.append(t)
            continue
        variants = t.get("variants") or []
        rids = []
        base = f"{HIVE.name}/candidates/{t['id']}"
        for i in range(1, k + 1):
            rid = f"{t['id']}.r{i}"
            rids.append(rid)
            hint = variants[(i - 1) % len(variants)] if variants else "an approach of your own choosing"
            r = dict(t)
            r.update(
                id=rid,
                replicas=1,
                origin=t["id"],
                title=f"{t['title']} (candidate {i}/{k})",
                writes=[f"{base}/r{i}/{w}" for w in t["writes"]],
                spec=(
                    f"{t['spec']}\n\nYou are candidate {i} of {k}, working independently. Approach: {hint}.\n"
                    f"Write your version to the candidate paths you own; they mirror the real paths "
                    f"under {base}/r{i}/. Do not touch the real paths."
                ),
                accept=[],
            )
            out.append(r)
        judge = {
            "id": t["id"],
            "role": "judge",
            "tier": t.get("judge_tier") or "deep",
            "deps": rids,
            "reads": [f"{base}/"] + t["reads"],
            "writes": t["writes"],
            "accept": t["accept"],
            "replicas": 1,
            "title": f"judge: {t['title']}",
            "spec": (
                f"{k} candidates were written under {base}/r1..r{k}/ (paths mirror the real ones).\n"
                f"Pick the best or merge the strongest parts, then write the final files to the real "
                f"paths you own.\n\nOriginal spec:\n{t['spec']}"
            ),
        }
        if t.get("model"):
            judge["model"] = t["model"]
        out.append(judge)
    return out


class Plan:
    def __init__(self, expand: bool = True):
        raw = read_plan_raw()
        self.raw = raw
        self.goal = str(raw.get("goal", "")).strip()
        self.budget = str(raw.get("budget", "balanced")).lower()
        if self.budget not in BUDGETS:
            die(f"unknown budget {self.budget!r}; use one of {', '.join(BUDGETS)}")
        b = BUDGETS[self.budget]
        self.models = dict(b["models"])
        if isinstance(raw.get("models"), dict):
            self.models.update({k.lower(): str(v).lower() for k, v in raw["models"].items()})
        self.max_parallel = int(raw.get("max_parallel") or b["max_parallel"])
        self.pack = int(raw.get("pack") or 1)
        self.agents = str(raw.get("agents", "general")).strip()
        acc = raw.get("accept") or []
        self.accept = [acc] if isinstance(acc, str) else list(acc)
        self.overhead = int(raw.get("agent_overhead_tokens") or 14000)
        tasks = expand_foreach([normalize_task(t) for t in raw.get("tasks", [])])
        self.tasks = expand_replicas(tasks) if expand else tasks
        self.by_id = {t["id"]: t for t in self.tasks}
        self._levels = self._anc = self._cl = None
        self._order: list = []
        self.children: dict = {t["id"]: [] for t in self.tasks}
        for t in self.tasks:
            for d in t["deps"]:
                if d in self.children:
                    self.children[d].append(t["id"])

    # ---- intelligence dial -------------------------------------------------
    def model_for(self, t: dict) -> str:
        if t.get("model"):
            return t["model"]
        if t["tier"] == "inherit":
            return "inherit"
        return self.models.get(t["tier"], "sonnet")

    def subagent_for(self, t: dict) -> str:
        style = self.agents
        if style in ("", "general", "general-purpose"):
            return "general-purpose"
        if style == "hive":
            return f"hive-{t['role']}"
        if style == "plugin":
            return f"hivemind:hive-{t['role']}"
        return f"{style}{t['role']}"  # custom prefix

    # ---- graph (iterative: safe for thousands of tasks) --------------------
    def topo_levels(self):
        """Return (levels dict id->wave, cycle list or None). Cached."""
        if self._levels is not None:
            return self._levels
        indeg = {t["id"]: 0 for t in self.tasks}
        for t in self.tasks:
            for d in t["deps"]:
                if d in self.by_id:
                    indeg[t["id"]] += 1
        level = {}
        order = []
        frontier = [i for i, n in indeg.items() if n == 0]
        for i in frontier:
            level[i] = 0
        while frontier:
            nxt = []
            for i in frontier:
                order.append(i)
                for c in self.children[i]:
                    level[c] = max(level.get(c, 0), level[i] + 1)
                    indeg[c] -= 1
                    if indeg[c] == 0:
                        nxt.append(c)
            frontier = nxt
        self._order = order
        cycle = sorted(i for i, n in indeg.items() if n > 0) if len(order) != len(self.tasks) else None
        self._levels = (level, cycle)
        return self._levels

    def ancestors(self) -> dict:
        if self._anc is None:
            self.topo_levels()
            anc: dict = {}
            for i in self._order:
                s = set()
                for d in self.by_id[i]["deps"]:
                    if d in anc:
                        s.add(d)
                        s |= anc[d]
                anc[i] = s
            for t in self.tasks:  # tasks on a cycle (validate reports them)
                anc.setdefault(t["id"], set())
            self._anc = anc
        return self._anc

    def critical_len(self) -> dict:
        """Weighted longest path from each task to a sink (critical-path priority)."""
        if self._cl is None:
            self.topo_levels()
            cl: dict = {}
            for i in reversed(self._order):
                w = TIER_WEIGHT.get(self.by_id[i]["tier"], 2)
                cl[i] = w + max((cl.get(c, 0) for c in self.children[i]), default=0)
            for t in self.tasks:
                cl.setdefault(t["id"], 0)
            self._cl = cl
        return self._cl


# --------------------------------------------------------------------------
# State: one JSON file per task, written atomically -> safe for parallel workers
# --------------------------------------------------------------------------
def status_path(tid: str) -> Path:
    return STATUS_DIR / f"{tid}.json"


def read_state(tid: str) -> dict:
    p = status_path(tid)
    if not p.exists():
        return {"state": "pending"}
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {"state": "pending"}


def write_state(tid: str, **fields) -> dict:
    STATUS_DIR.mkdir(parents=True, exist_ok=True)
    st = read_state(tid)
    if fields.get("state") == "running" and st.get("state") != "running":
        st["attempt"] = int(st.get("attempt", 0)) + 1
        st["started"] = time.time()
    st.update(fields)
    st["ts"] = time.time()
    tmp = status_path(tid).with_suffix(f".tmp{os.getpid()}")
    tmp.write_text(json.dumps(st, indent=1), encoding="utf-8")
    os.replace(tmp, status_path(tid))
    return st


def all_states(plan: Plan) -> dict:
    return {t["id"]: read_state(t["id"]) for t in plan.tasks}


def append_line(path: Path, line: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
    try:
        os.write(fd, (line.rstrip("\n") + "\n").encode("utf-8"))
    finally:
        os.close(fd)


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------
CONTEXT_TEMPLATE = """# Project context
<!-- Every worker reads this file. Keep it under ~1,500 words: it is paid for once per worker. -->

## Goal
{goal}

## Stack and conventions
- Language, framework and versions:
- Style: naming, formatting, error handling, logging:
- Test command:

## Layout
<!-- Directory tree of what exists and what will exist. -->

## Contracts
<!-- Shared types, API shapes and module boundaries that parallel workers code against.
     Put large contracts in real files (for example src/types.ts) and list them in `reads:`. -->

## Constraints
- No new dependencies unless a task says so.
"""

PLAN_TEMPLATE = """goal: {goal}
budget: {budget}
# agents: general    (general | hive | plugin | <custom-prefix>)
# accept: <global check run by `hive verify --run`>

## T1 [architect] Define contracts
writes: src/types.ts
Describe the shared types every builder codes against.

## T2 [builder] First module
writes: src/a.ts
reads: src/types.ts
deps: T1
accept: <command that proves this task works>
Implement ...
"""


def cmd_init(a) -> None:
    for d in (HIVE, HIVE / "bin", STATUS_DIR, REQ_DIR):
        d.mkdir(parents=True, exist_ok=True)
    src = Path(__file__).resolve()
    dst = (HIVE / "bin" / "hive.py").resolve()
    if src != dst:
        shutil.copy2(src, dst)
    goal = a.goal or "<one sentence>"
    if not CONTEXT.exists() or a.force:
        CONTEXT.write_text(CONTEXT_TEMPLATE.format(goal=goal), encoding="utf-8")
    if (not PLAN_MD.exists() and not PLAN_JSON.exists()) or a.force:
        PLAN_MD.write_text(PLAN_TEMPLATE.format(goal=goal, budget=a.budget), encoding="utf-8")
    gi = HIVE / ".gitignore"
    if not gi.exists():
        gi.write_text("# hive runtime state\nstatus/\nrequests/\ncandidates/\nbin/\nboard.md\n" if a.track else "*\n", encoding="utf-8")
    if a.force and STATUS_DIR.exists():
        for f in STATUS_DIR.glob("*.json"):
            f.unlink()
    print(f"hive {VERSION} ready at {HIVE}/  -> edit {CONTEXT} and {PLAN_MD}, then `{hive_cmd()} validate`")


def validate(plan: Plan, strict: bool = True):
    errors, warns = [], []
    if not plan.tasks:
        errors.append("plan has no tasks")
    seen = set()
    for t in plan.tasks:
        tid = t["id"]
        if not tid or not ID_RE.match(tid):
            errors.append(f"bad task id {tid!r} (letters, digits, _ . - only)")
        if tid in seen:
            errors.append(f"duplicate task id {tid}")
        seen.add(tid)
        if not t["spec"]:
            errors.append(f"{tid}: empty spec")
        if t["role"] not in ROLES:
            warns.append(f"{tid}: custom role {t['role']!r} (no built-in instructions)")
        if t["tier"] not in TIERS:
            errors.append(f"{tid}: tier {t['tier']!r} not in {TIERS}")
        if t.get("model") and t["model"] not in MODELS:
            errors.append(f"{tid}: model {t['model']!r} not in {MODELS}")
        for d in t["deps"]:
            if d == tid:
                errors.append(f"{tid}: depends on itself")
            elif d not in plan.by_id:
                errors.append(f"{tid}: unknown dep {d}")
        if t["role"] in ("builder", "scribe", "integrator", "judge", "architect") and not t["writes"]:
            warns.append(f"{tid}: {t['role']} owns no files")
        if len(t["writes"]) > 12:
            warns.append(f"{tid}: owns {len(t['writes'])} files; consider splitting for more parallelism")
        if len(t["spec"]) > 6000:
            warns.append(f"{tid}: spec is {len(t['spec'])} chars; move shared detail into context.md")
    if errors:
        return errors, warns
    _, cycle = plan.topo_levels()
    if cycle:
        errors.append("dependency cycle among: " + ", ".join(cycle))
        return errors, warns
    anc = plan.ancestors()
    # Indexed conflict check: exact paths via a dict, directory specs via prefix
    # scan, so thousands of owned paths validate in well under a second.
    by_path: dict = {}
    dirs: list = []
    for t in plan.tasks:
        for w in t["writes"]:
            by_path.setdefault(w, []).append(t["id"])
            if is_dir_spec(w):
                dirs.append((w, t["id"]))
    pairs = set()
    for w, owners_ in by_path.items():
        for x in range(len(owners_)):
            for y in range(x + 1, len(owners_)):
                pairs.add((owners_[x], owners_[y], w))
    if dirs:
        paths = sorted(by_path)
        import bisect
        for d, tid in dirs:
            k = bisect.bisect_left(paths, d)
            while k < len(paths) and paths[k].startswith(d):
                for other in by_path[paths[k]]:
                    if other != tid:
                        pairs.add((tid, other, d))
                k += 1
    reported = set()
    for ta, tb, w in sorted(pairs):
        if ta == tb or ta in anc[tb] or tb in anc[ta]:
            continue  # ordered: the later task edits after the earlier one finished
        key = tuple(sorted((ta, tb)))
        if key in reported:
            continue
        reported.add(key)
        errors.append(f"write conflict: {ta} and {tb} both own {w} and can run concurrently; add a dep or split ownership")
        if len(reported) >= 50:
            errors.append("... more write conflicts omitted")
            break
    if CONTEXT.exists():
        n = len(CONTEXT.read_text(encoding="utf-8"))
        if n > 12000:
            warns.append(f"context.md is {n} chars; every worker pays for it; trim to under ~8000")
    else:
        warns.append("context.md missing; workers get no shared context")
    return errors, warns


def cmd_validate(a) -> None:
    plan = Plan()
    errors, warns = validate(plan)
    for w in warns:
        print(f"warn: {w}")
    for e in errors:
        print(f"ERROR: {e}")
    if errors:
        sys.exit(1)
    levels, _ = plan.topo_levels()
    waves = max(levels.values()) + 1 if levels else 0
    cl = plan.critical_len()
    total = sum(TIER_WEIGHT.get(t["tier"], 2) for t in plan.tasks)
    crit = max(cl.values()) if cl else 1
    files = sum(len(t["writes"]) for t in plan.tasks)
    print(f"OK {len(plan.tasks)} tasks, {files} owned paths, {waves} waves, max_parallel {plan.max_parallel}, "
          f"parallel speedup up to ~{total / max(crit, 1):.1f}x vs serial")


def cmd_waves(a) -> None:
    plan = Plan()
    errors, _ = validate(plan)
    if errors:
        die("plan invalid; run validate")
    levels, _ = plan.topo_levels()
    states = all_states(plan)
    mark = {"done": "+", "running": "*", "failed": "x", "pending": "."}
    by_wave: dict = {}
    for t in plan.tasks:
        by_wave.setdefault(levels[t["id"]], []).append(t)
    for w in sorted(by_wave):
        cells = [f"{mark.get(states[t['id']]['state'], '?')}{t['id']}({t['role'][:5]}/{plan.model_for(t)})" for t in by_wave[w]]
        print(f"wave {w}: " + "  ".join(cells))


PROMPT_TEMPLATE = (
    "HIVE WORKER <IDS>. First run `{cmd} brief <IDS>` and follow it exactly; "
    "it contains your role, context, task(s), owned files and finish protocol. "
    "Your final reply must be only the one line the brief asks for."
)
NO_PACK_ROLES = ("architect", "integrator", "judge")


def worker_prompt(ids) -> str:
    ids = ids if isinstance(ids, str) else ",".join(ids)
    return PROMPT_TEMPLATE.format(cmd=hive_cmd()).replace("<IDS>", ids)


def make_lanes(plan: "Plan", ready: list, slots: int, pack: int) -> list:
    """Group ready tasks into worker lanes. A lane is one Agent call that runs
    several small tasks back to back, paying the per-worker overhead once.
    Lanes only grow as large as needed to launch every ready task now."""
    if slots <= 0 or not ready:
        return []
    solo = [t for t in ready if pack <= 1 or t["role"] in NO_PACK_ROLES]
    packable = [t for t in ready if t not in solo]
    lanes = [[t] for t in solo]
    if packable:
        free = max(1, slots - len(lanes))
        groups: dict = {}
        for t in packable:  # keep priority order inside each model group
            groups.setdefault((plan.model_for(t), plan.subagent_for(t)), []).append(t)
        n = len(packable)
        for g in groups.values():
            if n <= free:
                lanes += [[t] for t in g]  # everything fits: full parallelism, no packing
                continue
            share = max(1, round(free * len(g) / n))  # this group's fair share of free slots
            if -(-len(g) // pack) >= share:
                lanes += [g[i:i + pack] for i in range(0, len(g), pack)]  # full lanes; extras wait
            else:  # spread the group evenly over its share, lanes differ by at most one task
                base, extra = divmod(len(g), share)
                i = 0
                for k in range(share):
                    size = base + (1 if k >= share - extra else 0)
                    if size:
                        lanes.append(g[i:i + size])
                    i += size
    prio = plan.critical_len()
    lanes.sort(key=lambda lane: -max(prio[t["id"]] for t in lane))
    return lanes[:slots]


def cmd_dispatch(a) -> None:
    plan = Plan()
    errors, _ = validate(plan)
    if errors:
        for e in errors:
            print(f"ERROR: {e}")
        die("fix the plan before dispatching")
    states = all_states(plan)
    now = time.time()
    requeued = []
    for tid, st in states.items():
        if st["state"] != "running":
            continue
        stale = a.stale is not None and now - float(st.get("started", now)) > a.stale * 60
        if a.requeue or stale:
            states[tid] = write_state(tid, state="pending", note="requeued", lane="")
            requeued.append(tid)
    if a.retry_failed:
        for tid, st in states.items():
            if st["state"] == "failed":
                states[tid] = write_state(tid, state="pending", note="retry", lane="")
                requeued.append(tid)

    def s(i):
        return states[i]["state"]

    running = [t["id"] for t in plan.tasks if s(t["id"]) == "running"]
    busy_lanes = {states[i].get("lane") or i for i in running}
    done = [t["id"] for t in plan.tasks if s(t["id"]) == "done"]
    failed = [t["id"] for t in plan.tasks if s(t["id"]) == "failed"]
    anc = plan.ancestors()
    failed_set = set(failed)
    blocked = [t["id"] for t in plan.tasks if s(t["id"]) == "pending" and anc[t["id"]] & failed_set]
    ready = [t for t in plan.tasks if s(t["id"]) == "pending" and all(s(d) == "done" for d in t["deps"])]
    cl = plan.critical_len()
    levels, _ = plan.topo_levels()
    ready.sort(key=lambda t: (-cl[t["id"]], levels[t["id"]]))
    limit = a.max if a.max else plan.max_parallel
    slots = max(0, limit - len(busy_lanes))
    pack = a.pack if a.pack else plan.pack
    lanes = make_lanes(plan, ready, slots, pack)
    launched = sum(len(x) for x in lanes)

    def mark():
        if a.dry:
            return
        for lane in lanes:
            lane_id = ",".join(t["id"] for t in lane)
            for t in lane:
                write_state(t["id"], state="running", note="dispatched", lane=lane_id)

    def call(lane):
        ids = [t["id"] for t in lane]
        t0 = lane[0]
        title = t0["title"] if len(lane) == 1 else f"{len(lane)} tasks"
        c = {"ids": ids, "description": f"hive {','.join(ids)} {title}"[:60], "subagent_type": plan.subagent_for(t0), "prompt": worker_prompt(ids)}
        m = plan.model_for(t0)
        if m != "inherit":
            c["model"] = m
        return c

    if a.json:
        out = [call(lane) for lane in lanes]
        for c in out:
            c["id"] = c["ids"][0]
        mark()
        print(json.dumps({"launch": out, "running": running, "done": len(done), "total": len(plan.tasks), "failed": failed, "blocked": blocked}, indent=1))
        return

    head = f"HIVE {len(done)}/{len(plan.tasks)} done, {len(running)} running in {len(busy_lanes)} workers"
    if failed:
        head += f", {len(failed)} failed ({', '.join(failed[:10])}{' ...' if len(failed) > 10 else ''})"
    if requeued:
        head += f", requeued {', '.join(requeued[:10])}" + (f" (+{len(requeued) - 10} more)" if len(requeued) > 10 else "")
    print(head)
    if not lanes:
        if len(done) == len(plan.tasks):
            print(f"ALL DONE. Next: `{hive_cmd()} verify --run`")
        elif running:
            print("Nothing new is ready. Wait for running workers, then dispatch again.")
        elif failed:
            print(f"Blocked by failures ({len(blocked)} tasks waiting). Read the notes with `{hive_cmd()} status --full`, "
                  f"fix the plan or reset, then use `dispatch --retry-failed`.")
        else:
            print("Nothing ready and nothing running; tasks may be stuck in `running`. Use `dispatch --requeue`.")
        return
    more = len(ready) - launched
    tail = f"; {more} more ready after slots free" if more > 0 else ""
    compact = len(lanes) > 6 and not a.verbose
    if compact:
        print(f"LAUNCH {len(lanes)} Agent calls ({launched} tasks) in ONE message (parallel){tail}.")
        print("Every prompt is this template with <IDS> replaced by the lane's ids, verbatim:")
        print(f"  {PROMPT_TEMPLATE.format(cmd=hive_cmd())}")
        for n, lane in enumerate(lanes, 1):
            c = call(lane)
            print(f"[{n}] <IDS>={','.join(c['ids'])}  model: {c.get('model', '(omit: inherit)')}  "
                  f"subagent_type: {c['subagent_type']}  description: {c['description']}")
    else:
        print(f"LAUNCH {len(lanes)} Agent calls ({launched} tasks) in ONE message (parallel){tail}:")
        for n, lane in enumerate(lanes, 1):
            c = call(lane)
            print(f"[{n}] description: {c['description']}")
            print(f"    subagent_type: {c['subagent_type']}   model: {c.get('model', '(omit: inherit)')}")
            print(f"    prompt: {c['prompt']}")
    mark()


def _task_section(plan: "Plan", t: dict, multi: bool) -> list:
    h = "###" if multi else "##"
    out = []
    if multi:
        out += ["", f"## Task {t['id']}: {t['title']}", f"role: {t['role']} | tier: {t['tier']}"]
    out += ["", f"{h} Your task" if not multi else f"{h} Spec", t["spec"]]
    out += ["", f"{h} Files you own (exclusive write access)"]
    out += [f"- {w}" for w in t["writes"]] or ["- (none: do not modify project files; report in your done note)"]
    if t["reads"]:
        out += ["", f"{h} Read these first"] + [f"- {r}" for r in t["reads"]]
    ups = [plan.by_id[d] for d in t["deps"] if d in plan.by_id]
    if ups:
        out += ["", f"{h} Upstream results (already finished)"]
        shown = ups[:25]
        for u in shown:
            us = read_state(u["id"])
            files = ", ".join(u["writes"][:8]) + (" ..." if len(u["writes"]) > 8 else "")
            out.append(f"- {u['id']} {u['title']}: {us.get('note', '')}" + (f" | files: {files}" if files else ""))
        if len(ups) > len(shown):
            out.append(f"- ... and {len(ups) - len(shown)} more upstream tasks (all done)")
    if t["accept"]:
        out += ["", f"{h} Acceptance (run these from the project root and make them pass before finishing)"]
        out += [f"- `{c}`" for c in t["accept"]]
    return out


def cmd_brief(a) -> None:
    plan = Plan()
    ids = [x.strip() for x in a.id.split(",") if x.strip()]
    for i in ids:
        if i not in plan.by_id:
            die(f"unknown task {i}")
    todo, already = [], []
    for i in ids:
        st = read_state(i)
        (already if st["state"] == "done" and not a.peek else todo).append(i)
    if not todo:
        print(f"{','.join(ids)} already done. Reply: `{','.join(ids)} already done`.")
        return
    if not a.peek:
        for i in todo:
            write_state(i, state="running", note="working")
    tasks = [plan.by_id[i] for i in todo]
    multi = len(tasks) > 1
    t0 = tasks[0]
    if multi:
        out = [f"# HIVE BRIEF {','.join(todo)}: {len(tasks)} tasks. Do them in order; finish each one (run `done`) before starting the next.",
               f"plan goal: {plan.goal or '-'}"]
    else:
        out = [f"# HIVE BRIEF {t0['id']}: {t0['title']}", f"role: {t0['role']} | tier: {t0['tier']} | plan goal: {plan.goal or '-'}"]
    if already:
        out.append(f"(already done, skip: {', '.join(already)})")
    if root_prefix():
        out.append(f"Project root: `{root_prefix()}/`. Every path in this brief is relative to it; run commands with `cd {root_prefix()} && ...`.")
    roles = list(dict.fromkeys(t["role"] for t in tasks))
    out += ["", "## Role" if len(roles) == 1 else "## Roles"]
    for r in roles:
        text = ROLES.get(r, ("", f"Custom role: {r}. Follow the spec."))[1]
        out.append(text if len(roles) == 1 else f"- {r}: {text}")
    if CONTEXT.exists():
        ctx = re.sub(r"<!--.*?-->\n?", "", CONTEXT.read_text(encoding="utf-8"), flags=re.S).strip()
        out += ["", "## Project context (shared by all workers)", ctx]
    for t in tasks:
        out += _task_section(plan, t, multi)
    if BOARD.exists():
        lines = [x for x in BOARD.read_text(encoding="utf-8").splitlines() if x.strip()][-30:]
        if lines:
            out += ["", "## Team board (decisions other workers posted)"] + lines
    hc = hive_cmd()
    me = t0["id"]
    out += [
        "",
        "## Rules",
        "- Only create or modify the files you own. Other workers are editing other files at the same time.",
        f"- Need a change in a file you do not own? Record it and continue: `{hc} ask {me} \"<path>: <change and why>\"`",
        f"- Made a decision other workers must follow (a name, format or port)? Post it: `{hc} post {me} \"<decision>\"`",
        "- Do not run git commands that change state (commit, checkout, stash, reset, push); the orchestrator owns git.",
        "- Do not re-read files you have no reason to read. Do not explain your work in prose.",
        "",
        "## Finish",
    ]
    if multi:
        out += [
            f"1. After EACH task: `{hc} done <id> -m \"<=20 words>\"` (or `{hc} fail <id> -m \"<why>\"`), then start the next.",
            "   Finishing each task promptly lets downstream work start sooner.",
            f"2. Your final reply must be exactly one line: `{','.join(todo)}: <id> ok|FAIL; ...` (for example `{todo[0]} ok; {todo[1]} ok`).",
        ]
    else:
        out += [
            f"1. Success: `{hc} done {me} -m \"<=20 words: what exists now>\"`",
            f"   Cannot finish: `{hc} fail {me} -m \"<=20 words: why>\"`",
            f"2. Your final reply must be exactly one line: `{me} ok: <same note>` or `{me} FAIL: <reason>`.",
        ]
    print("\n".join(out))


def cmd_done(a) -> None:
    plan = Plan()
    t = plan.by_id.get(a.id)
    if not t:
        die(f"unknown task {a.id}")
    missing = [w for w in t["writes"] if not is_dir_spec(w) and not at_root(w).exists()]
    if missing and not a.force:
        die(f"{a.id}: owned files missing: {', '.join(missing)}. Create them, or use `fail`, or `done --force`.")
    write_state(a.id, state="done", note=(a.m or "").strip()[:300])
    print(f"{a.id} recorded done")


def cmd_fail(a) -> None:
    plan = Plan()
    if a.id not in plan.by_id:
        die(f"unknown task {a.id}")
    write_state(a.id, state="failed", note=(a.m or "").strip()[:300])
    print(f"{a.id} recorded failed")


def cmd_ask(a) -> None:
    append_line(REQ_DIR / f"{a.id}.txt", f"{a.id}: {a.text}")
    print("request recorded; continue your task")


def cmd_post(a) -> None:
    append_line(BOARD, f"- [{a.id}] {a.text}")
    print("posted")


def read_requests() -> list:
    if not REQ_DIR.exists():
        return []
    out = []
    for f in sorted(REQ_DIR.glob("*.txt")):
        out += [x for x in f.read_text(encoding="utf-8").splitlines() if x.strip()]
    return out


def cmd_status(a) -> None:
    if not (PLAN_MD.exists() or PLAN_JSON.exists()):
        if a.line:
            return
        die("no hive plan in this directory")
    plan = Plan()
    states = all_states(plan)
    counts: dict = {}
    for st in states.values():
        counts[st["state"]] = counts.get(st["state"], 0) + 1
    total = len(plan.tasks)
    done = counts.get("done", 0)
    reqs = read_requests()
    if a.line:
        if done == total:
            return
        print(f"[hive] active plan: {done}/{total} done, {counts.get('running', 0)} running, "
              f"{counts.get('failed', 0)} failed. Resume with /hive-resume or `{hive_cmd()} dispatch`.")
        return
    print(f"HIVE {plan.goal[:70] or '(no goal)'} | budget {plan.budget} | {done}/{total} done, "
          f"{counts.get('running', 0)} running, {counts.get('failed', 0)} failed, {counts.get('pending', 0)} pending")
    levels, cycle = plan.topo_levels()
    if cycle:
        print("plan has a cycle; run validate")
        return
    mark = {"done": "+", "running": "*", "failed": "x", "pending": "."}
    by_wave: dict = {}
    for t in plan.tasks:
        by_wave.setdefault(levels[t["id"]], []).append(t["id"])
    for w in sorted(by_wave):
        ids = by_wave[w]
        if len(ids) <= 24 or a.full:
            print(f" w{w}: " + " ".join(f"{mark.get(states[i]['state'], '?')}{i}" for i in ids))
        else:  # large waves: counts plus only the ids that need attention
            c: dict = {}
            for i in ids:
                c[states[i]["state"]] = c.get(states[i]["state"], 0) + 1
            hot = [f"{mark[states[i]['state']]}{i}" for i in ids if states[i]["state"] in ("running", "failed")][:20]
            print(f" w{w}: {c.get('done', 0)}/{len(ids)} done, {c.get('running', 0)} running, {c.get('failed', 0)} failed"
                  + (f" | {' '.join(hot)}" if hot else ""))
    if a.full:
        for t in plan.tasks:
            st = states[t["id"]]
            print(f"  {t['id']:<10} {st['state']:<8} {t['role']:<10} {plan.model_for(t):<8} {st.get('note', '')[:90]}")
    else:
        for t in plan.tasks:
            st = states[t["id"]]
            if st["state"] == "failed":
                print(f" FAIL {t['id']}: {st.get('note', '')}")
    if reqs:
        print(f" requests ({len(reqs)}):")
        for r in reqs[-20:]:
            print(f"  {r}")


def cmd_verify(a) -> None:
    plan = Plan()
    states = all_states(plan)
    bad = 0
    for t in plan.tasks:
        if states[t["id"]]["state"] != "done":
            print(f"NOT DONE {t['id']} ({states[t['id']]['state']})")
            bad += 1
            continue
        for w in t["writes"]:
            if not is_dir_spec(w) and not at_root(w).exists():
                print(f"MISSING {w} (owned by {t['id']})")
                bad += 1
    if a.run:
        cmds, seen = [], set()
        for t in plan.tasks:
            if t.get("origin"):
                continue
            for c in t["accept"]:
                if c not in seen:
                    seen.add(c)
                    cmds.append((t["id"], c))
        for c in plan.accept:
            if c not in seen:
                seen.add(c)
                cmds.append(("plan", c))
        for owner, c in cmds:
            try:
                r = subprocess.run(c, shell=True, capture_output=True, text=True, timeout=a.timeout, cwd=str(ROOT))
                ok, output = r.returncode == 0, (r.stdout + r.stderr)
            except subprocess.TimeoutExpired:
                ok, output = False, f"timed out after {a.timeout}s"
            if ok:
                print(f"PASS [{owner}] {c}")
            else:
                bad += 1
                tail = "\n".join(output.strip().splitlines()[-25:])
                print(f"FAIL [{owner}] {c}\n{tail}")
    print("VERIFY OK" if not bad else f"VERIFY: {bad} problem(s)")
    sys.exit(1 if bad else 0)


def cmd_reset(a) -> None:
    plan = Plan()
    ids = list(a.ids)
    if a.failed:
        ids += [i for i, s in all_states(plan).items() if s["state"] == "failed"]
    if a.all:
        ids = [t["id"] for t in plan.tasks]
    for i in ids:
        p = status_path(i)
        if p.exists():
            p.unlink()
    if a.all:
        for d in (REQ_DIR,):
            if d.exists():
                for f in d.glob("*.txt"):
                    f.unlink()
        if BOARD.exists():
            BOARD.unlink()
    print(f"reset {len(ids)} task(s)")


def _size_tokens(p: str) -> int:
    path = at_root(p)
    try:
        if path.is_file():
            return path.stat().st_size // 4
        if path.is_dir():
            return sum(f.stat().st_size for f in path.rglob("*") if f.is_file()) // 4
    except OSError:
        pass
    return 0


def cmd_estimate(a) -> None:
    """Two numbers per task: unique tokens (what the worker actually has to read or
    write once) and cumulative tokens (unique context re-read on every tool turn,
    which is what usage meters count; most of it is billed as cheap cache reads).
    Calibrated on real runs: a 1-3 file worker takes ~4-7 turns, 40-120k cumulative."""
    plan = Plan()
    ctx = len(CONTEXT.read_text(encoding="utf-8")) // 4 if CONTEXT.exists() else 0
    per_model: dict = {}
    rows = []
    for t in plan.tasks:
        brief = ctx + (len(t["spec"]) + 1500) // 4
        reads = sum(_size_tokens(r) for r in t["reads"])
        files = [w for w in t["writes"] if not is_dir_spec(w)]
        outp = sum(max(_size_tokens(w), 900) for w in files) or 600
        unique = plan.overhead + brief + reads + outp
        turns = 2 + max(1, len(files))
        cumulative = turns * (plan.overhead + brief) + (turns // 2) * (reads + outp)
        m = plan.model_for(t)
        pm = per_model.setdefault(m, [0, 0, 0, 0])
        pm[0] += 1
        pm[1] += unique
        pm[2] += cumulative
        pm[3] += outp
        rows.append((t["id"], m, unique, cumulative, outp))
    pack = max(1, a.pack or plan.pack)
    packable = sum(1 for t in plan.tasks if t["role"] not in NO_PACK_ROLES)
    workers = (len(plan.tasks) - packable) + -(-packable // pack)
    extra = len(plan.tasks) - workers
    saved_u = extra * plan.overhead
    saved_c = extra * 3 * plan.overhead  # the fixed turns a separate worker would spend booting
    orch = workers * 150 + 2000
    if a.full:
        for r in rows:
            print(f"  {r[0]:<14} {r[1]:<8} unique~{r[2]:>8,} cumulative~{r[3]:>9,} out~{r[4]:>6,}")
    for m, (n, u, c, o) in sorted(per_model.items()):
        print(f"{m:<8} {n:>4} tasks  unique~{u:>10,}  cumulative~{c:>11,}  out~{o:>9,}")
    tu = sum(r[2] for r in rows) - saved_u
    tc = sum(r[3] for r in rows) - saved_c
    to = sum(r[4] for r in rows)
    print(f"orchestrator ~{orch:,} tokens ({workers} workers x ~150 for dispatch line + one-line report)")
    if pack > 1:
        print(f"packing {pack}: {len(plan.tasks)} tasks in ~{workers} workers, saves ~{saved_u:,} unique / ~{saved_c:,} cumulative")
    print(f"TOTAL unique ~{tu + orch:,} | cumulative ~{tc + orch:,} (mostly prompt-cache reads) | output ~{to:,}")
    cl = plan.critical_len()
    crit = max(cl.values()) if cl else 1
    total_w = sum(TIER_WEIGHT.get(t["tier"], 2) for t in plan.tasks)
    print(f"wall-clock: critical path {crit} vs serial {total_w} weight units "
          f"(~{total_w / max(crit, 1):.1f}x faster, capped by max_parallel {plan.max_parallel})")


def cmd_board(a) -> None:
    print(BOARD.read_text(encoding="utf-8") if BOARD.exists() else "(board empty)")


def main(argv=None) -> None:
    p = argparse.ArgumentParser(prog="hive", description="Multi-agent orchestration kernel for Claude Code")
    p.add_argument("--version", action="version", version=f"hive {VERSION}")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("init", help="create .hive/ with templates and a local copy of this tool")
    s.add_argument("--goal", default="")
    s.add_argument("--budget", default="balanced", choices=list(BUDGETS))
    s.add_argument("--force", action="store_true", help="overwrite templates and clear status")
    s.add_argument("--track", action="store_true", help="let git track plan.md and context.md")
    s.set_defaults(fn=cmd_init)

    sub.add_parser("validate", help="check DAG, ownership conflicts, sizes").set_defaults(fn=cmd_validate)
    sub.add_parser("waves", help="show tasks by wave with model assignment").set_defaults(fn=cmd_waves)

    s = sub.add_parser("dispatch", help="mark ready tasks running and print Agent calls")
    s.add_argument("--dry", action="store_true", help="print without marking running")
    s.add_argument("--json", action="store_true")
    s.add_argument("--max", type=int, default=0, help="override max_parallel for this call")
    s.add_argument("--requeue", action="store_true", help="requeue every task stuck in running")
    s.add_argument("--stale", type=float, default=None, metavar="MIN", help="requeue running tasks older than MIN minutes")
    s.add_argument("--retry-failed", action="store_true")
    s.add_argument("--pack", type=int, default=0, metavar="N", help="let one worker run up to N small tasks (overrides plan `pack:`)")
    s.add_argument("--verbose", action="store_true", help="full per-call output even for large batches")
    s.set_defaults(fn=cmd_dispatch)

    s = sub.add_parser("brief", help="(worker) print the assignment for one id or a comma list")
    s.add_argument("id")
    s.add_argument("--peek", action="store_true", help="show without changing state")
    s.set_defaults(fn=cmd_brief)

    for name, fn in (("done", cmd_done), ("fail", cmd_fail)):
        s = sub.add_parser(name, help=f"(worker) record task {name}")
        s.add_argument("id")
        s.add_argument("-m", default="", help="short note")
        if name == "done":
            s.add_argument("--force", action="store_true", help="accept even if owned files are missing")
        s.set_defaults(fn=fn)

    s = sub.add_parser("ask", help="(worker) request a change in a file you do not own")
    s.add_argument("id")
    s.add_argument("text")
    s.set_defaults(fn=cmd_ask)

    s = sub.add_parser("post", help="(worker) post a decision to the team board")
    s.add_argument("id")
    s.add_argument("text")
    s.set_defaults(fn=cmd_post)

    sub.add_parser("board", help="print the team board").set_defaults(fn=cmd_board)

    s = sub.add_parser("status", help="compact progress view")
    s.add_argument("--full", action="store_true")
    s.add_argument("--line", action="store_true", help="one line, silent when idle (for hooks)")
    s.set_defaults(fn=cmd_status)

    s = sub.add_parser("verify", help="check owned files exist; --run executes acceptance commands")
    s.add_argument("--run", action="store_true")
    s.add_argument("--timeout", type=int, default=600)
    s.set_defaults(fn=cmd_verify)

    s = sub.add_parser("reset", help="return tasks to pending")
    s.add_argument("ids", nargs="*")
    s.add_argument("--failed", action="store_true")
    s.add_argument("--all", action="store_true")
    s.set_defaults(fn=cmd_reset)

    s = sub.add_parser("estimate", help="rough token and wall-clock estimate")
    s.add_argument("--full", action="store_true")
    s.add_argument("--pack", type=int, default=0, metavar="N", help="estimate as if packing N tasks per worker")
    s.set_defaults(fn=cmd_estimate)

    args = p.parse_args(argv)
    args.fn(args)


if __name__ == "__main__":
    main()
