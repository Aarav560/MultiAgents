"""End-to-end tests for the hive kernel. Run: python3 -m unittest discover tests"""
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HIVE = Path(__file__).resolve().parents[1] / "skills" / "hive" / "scripts" / "hive.py"


class HiveCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.run_hive("init", "--goal", "test")

    def tearDown(self):
        self._tmp.cleanup()

    def run_hive(self, *args, ok=True, local=False):
        script = self.dir / ".hive" / "bin" / "hive.py" if local else HIVE
        env = {k: v for k, v in os.environ.items() if k != "HIVE_DIR"}
        r = subprocess.run([sys.executable, str(script), *args], cwd=self.dir, capture_output=True, text=True, env=env)
        if ok and r.returncode != 0:
            self.fail(f"hive {' '.join(args)} failed ({r.returncode}):\n{r.stdout}\n{r.stderr}")
        return r

    def plan(self, text):
        (self.dir / ".hive" / "plan.md").write_text(textwrap.dedent(text))

    def touch(self, *paths):
        for p in paths:
            f = self.dir / p
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text("hello\n")

    def state(self, tid):
        return json.loads((self.dir / ".hive" / "status" / f"{tid}.json").read_text())["state"]


class TestInitAndParse(HiveCase):
    def test_init_creates_layout_and_local_copy(self):
        for p in ("context.md", "plan.md", "bin/hive.py", ".gitignore"):
            self.assertTrue((self.dir / ".hive" / p).exists(), p)
        self.run_hive("validate", local=True)

    def test_plan_md_parsing(self):
        self.plan("""\
            goal: G
            budget: lean
            max_parallel: 3
            accept: echo global

            ## A [architect opus] Contracts
            writes: src/types.ts, src/api.ts
            Design the types.
            model: the User record has an id

            ## B [builder fast] Build
            writes: src/b.ts
            reads: src/types.ts
            deps: A
            accept: echo b
            Build it.
            """)
        r = self.run_hive("dispatch", "--json", "--dry")
        data = json.loads(r.stdout)
        self.assertEqual(data["total"], 2)
        self.assertEqual(data["launch"][0]["id"], "A")
        self.assertEqual(data["launch"][0]["model"], "opus")
        brief = self.run_hive("brief", "A", "--peek").stdout
        self.assertIn("model: the User record has an id", brief)  # prose, not a key
        self.assertIn("- src/types.ts", brief)

    def test_json_plan_supported(self):
        (self.dir / ".hive" / "plan.md").unlink()
        (self.dir / ".hive" / "plan.json").write_text(json.dumps({
            "goal": "J", "tasks": [{"id": "X", "spec": "do x", "writes": ["x.txt"]}]}))
        self.assertIn("OK 1 tasks", self.run_hive("validate").stdout)


class TestValidation(HiveCase):
    def test_write_conflict_detected(self):
        self.plan("""\
            ## A
            writes: src/x.ts
            a
            ## B
            writes: src/x.ts
            b
            """)
        r = self.run_hive("validate", ok=False)
        self.assertEqual(r.returncode, 1)
        self.assertIn("write conflict", r.stdout)

    def test_directory_ownership_conflict(self):
        self.plan("""\
            ## A
            writes: src/
            a
            ## B
            writes: src/deep/y.ts
            b
            """)
        self.assertIn("write conflict", self.run_hive("validate", ok=False).stdout)

    def test_ordered_writes_are_allowed(self):
        self.plan("""\
            ## A
            writes: src/x.ts
            a
            ## B
            writes: src/x.ts
            deps: A
            b
            """)
        self.run_hive("validate")

    def test_cycle_detected(self):
        self.plan("""\
            ## A
            deps: B
            writes: a
            a
            ## B
            deps: A
            writes: b
            b
            """)
        self.assertIn("cycle", self.run_hive("validate", ok=False).stdout)

    def test_unknown_dep(self):
        self.plan("""\
            ## A
            deps: NOPE
            writes: a
            a
            """)
        self.assertIn("unknown dep", self.run_hive("validate", ok=False).stdout)


class TestLifecycle(HiveCase):
    PLAN = """\
        goal: life
        max_parallel: 2
        ## T1 [architect]
        writes: t1.txt
        one
        ## T2
        writes: t2.txt
        deps: T1
        two
        ## T3
        writes: t3.txt
        deps: T1
        three
        ## T4
        writes: t4.txt
        deps: T1
        four
        ## T5 [integrator]
        writes: t5.txt
        deps: T2, T3, T4
        accept: test -f t5.txt
        five
        """

    def test_full_run(self):
        self.plan(self.PLAN)
        out = self.run_hive("dispatch").stdout
        self.assertIn("LAUNCH 1", out)
        self.assertEqual(self.state("T1"), "running")
        # done refuses when owned files are missing
        r = self.run_hive("done", "T1", "-m", "x", ok=False)
        self.assertIn("owned files missing", r.stderr)
        self.touch("t1.txt")
        self.run_hive("done", "T1", "-m", "contracts ready", local=True)
        out = self.run_hive("dispatch").stdout
        self.assertIn("LAUNCH 2", out)  # max_parallel 2 caps the 3 ready tasks
        self.assertIn("1 more ready", out)
        brief = self.run_hive("brief", "T2", local=True).stdout
        self.assertIn("T1", brief)
        self.assertIn("contracts ready", brief)  # upstream note flows downstream
        self.run_hive("post", "T2", "port is 8080", local=True)
        self.run_hive("ask", "T2", "t1.txt: add field", local=True)
        for t in ("T2", "T3"):
            self.touch(f"{t.lower()}.txt")
            self.run_hive("done", t, "-m", "ok")
        self.assertIn("LAUNCH 1", self.run_hive("dispatch").stdout)
        self.assertIn("port is 8080", self.run_hive("brief", "T4").stdout)  # board shared
        self.touch("t4.txt")
        self.run_hive("done", "T4", "-m", "ok")
        self.run_hive("dispatch")
        status = self.run_hive("status").stdout
        self.assertIn("4/5 done", status)
        self.assertIn("t1.txt: add field", status)
        self.touch("t5.txt")
        self.run_hive("done", "T5", "-m", "wired")
        self.assertIn("ALL DONE", self.run_hive("dispatch").stdout)
        self.assertIn("VERIFY OK", self.run_hive("verify", "--run").stdout)
        self.assertEqual(self.run_hive("status", "--line").stdout, "")

    def test_failure_blocks_and_retry(self):
        self.plan(self.PLAN)
        self.run_hive("dispatch")
        self.run_hive("fail", "T1", "-m", "missing spec")
        out = self.run_hive("dispatch").stdout
        self.assertIn("Blocked by failures", out)
        self.assertIn("LAUNCH 1", self.run_hive("dispatch", "--retry-failed").stdout)
        self.assertEqual(self.state("T1"), "running")

    def test_requeue_stuck_running(self):
        self.plan(self.PLAN)
        self.run_hive("dispatch")
        self.assertIn("Wait for running", self.run_hive("dispatch").stdout)
        out = self.run_hive("dispatch", "--requeue").stdout
        self.assertIn("requeued T1", out)
        self.assertIn("LAUNCH 1", out)

    def test_verify_run_reports_failure(self):
        self.plan("""\
            accept: exit 3
            ## A
            writes: a.txt
            a
            """)
        self.run_hive("dispatch")
        self.touch("a.txt")
        self.run_hive("done", "A")
        r = self.run_hive("verify", "--run", ok=False)
        self.assertIn("FAIL [plan] exit 3", r.stdout)


class TestReplicas(HiveCase):
    def test_replicas_expand_to_candidates_and_judge(self):
        self.plan("""\
            ## A [builder x3]
            writes: src/algo.py
            variants: fast | simple | clever
            accept: python3 -c "print(1)"
            Write the algorithm.
            ## B
            writes: src/use.py
            deps: A
            b
            """)
        data = json.loads(self.run_hive("dispatch", "--json").stdout)
        ids = [c["id"] for c in data["launch"]]
        self.assertEqual(ids, ["A.r1", "A.r2", "A.r3"])
        brief = self.run_hive("brief", "A.r2").stdout
        self.assertIn(".hive/candidates/A/r2/src/algo.py", brief)
        self.assertIn("Approach: simple", brief)
        for i in (1, 2, 3):
            self.touch(f".hive/candidates/A/r{i}/src/algo.py")
            self.run_hive("done", f"A.r{i}", "-m", f"cand {i}")
        data = json.loads(self.run_hive("dispatch", "--json").stdout)
        self.assertEqual([c["id"] for c in data["launch"]], ["A"])
        self.assertEqual(data["launch"][0]["model"], "opus")
        judge = self.run_hive("brief", "A").stdout
        self.assertIn("role: judge", judge)
        self.assertIn("cand 2", judge)


class TestDialAndAgents(HiveCase):
    def test_budget_and_inherit_and_agent_styles(self):
        self.plan("""\
            budget: max
            agents: plugin
            ## A [scout]
            writes: .hive/findings/a.md
            look
            ## B [inherit]
            writes: b
            build
            """)
        data = json.loads(self.run_hive("dispatch", "--json", "--dry").stdout)
        calls = {c["id"]: c for c in data["launch"]}
        self.assertEqual(calls["A"]["model"], "sonnet")  # max budget lifts fast tier
        self.assertEqual(calls["A"]["subagent_type"], "hivemind:hive-scout")
        self.assertNotIn("model", calls["B"])  # inherit = the session's own model

    def test_estimate_runs(self):
        self.assertIn("TOTAL", self.run_hive("estimate").stdout)


class TestScale(HiveCase):
    def test_foreach_glob_range_and_fan_in(self):
        self.touch("src/alpha.c", "src/beta.c", "src/gamma.c")
        self.plan("""\
            ## T-{stem} [builder]
            foreach: glob:src/*.c
            writes: tests/test_{stem}.c
            reads: {item}
            Test {name}; braces in code are safe: int f(void) { return 0; }
            ## P [scribe]
            foreach: range:1..4
            writes: docs/page{i}.md
            Page {i}.
            ## FIN [integrator]
            writes: Makefile
            deps: T-{stem}, P
            wire
            """)
        self.assertIn("OK 8 tasks", self.run_hive("validate").stdout)
        brief = self.run_hive("brief", "T-beta", "--peek").stdout
        self.assertIn("- tests/test_beta.c", brief)
        self.assertIn("- src/beta.c", brief)
        self.assertIn("int f(void) { return 0; }", brief)
        self.assertIn("- docs/page3.md", self.run_hive("brief", "P-3", "--peek").stdout)
        waves = self.run_hive("waves").stdout
        self.assertIn("wave 1: .FIN", waves)  # FIN waits for all 7 expansions

    def test_packing_lanes_multi_brief(self):
        self.plan("""\
            max_parallel: 3
            pack: 4
            ## W [builder]
            foreach: range:1..10
            writes: out/w{i}.txt
            Write {i}.
            ## A [architect]
            writes: arch.md
            design
            """)
        data = json.loads(self.run_hive("dispatch", "--json").stdout)
        lanes = [c["ids"] for c in data["launch"]]
        self.assertEqual(len(lanes), 3)
        self.assertIn(["A"], lanes)  # architects never packed
        self.assertEqual(sum(len(x) for x in lanes), 1 + 8)  # 2 free slots x lane size 4
        packed = next(x for x in lanes if len(x) > 1)
        brief = self.run_hive("brief", ",".join(packed)).stdout
        self.assertIn(f"{len(packed)} tasks. Do them in order", brief)
        self.assertIn(f"## Task {packed[1]}", brief)
        self.assertEqual(brief.count("## Project context"), 1)  # shared once
        # 3 lanes busy -> no free slots even though tasks are ready
        self.assertIn("Nothing new is ready", self.run_hive("dispatch").stdout)
        for i in packed:
            n = i.split("-")[1]
            self.touch(f"out/w{n}.txt")
            self.run_hive("done", i, "-m", "ok")
        out = self.run_hive("dispatch").stdout
        self.assertIn("LAUNCH 1 Agent calls", out)

    def test_compact_dispatch_for_big_batches(self):
        self.plan("""\
            max_parallel: 20
            ## W [scribe]
            foreach: range:1..12
            writes: out/{i}.txt
            Write {i}.
            """)
        out = self.run_hive("dispatch").stdout
        self.assertIn("LAUNCH 12 Agent calls (12 tasks)", out)
        self.assertIn("<IDS>=W-7", out)
        self.assertEqual(out.count("First run"), 1)  # template printed once

    def test_project_subdirectory_root(self):
        sub = self.dir / "demo"
        sub.mkdir()
        env = {k: v for k, v in os.environ.items() if k != "HIVE_DIR"}
        subprocess.run([sys.executable, str(HIVE), "init"], cwd=sub, check=True, capture_output=True, env=env)
        (sub / ".hive" / "plan.md").write_text("accept: test -f a.txt\n## A\nwrites: a.txt\na\n")
        local = ["python3", "demo/.hive/bin/hive.py"]
        r = subprocess.run(local + ["brief", "A"], cwd=self.dir, capture_output=True, text=True, env=env)
        self.assertIn("Project root: `demo/`", r.stdout)
        self.assertIn("python3 demo/.hive/bin/hive.py done A", r.stdout)
        (sub / "a.txt").write_text("x")
        r = subprocess.run(local + ["done", "A"], cwd=self.dir, capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0, r.stderr)
        r = subprocess.run(local + ["verify", "--run"], cwd=self.dir, capture_output=True, text=True, env=env)
        self.assertIn("VERIFY OK", r.stdout)

    def test_thousand_task_plan_is_fast(self):
        lines = ["max_parallel: 50", "pack: 5", "## ROOT [architect]", "writes: contracts/", "root", ""]
        for i in range(1000):
            lines += [f"## M{i} [builder fast]", f"writes: mod/m{i}.c, mod/m{i}.h", "deps: ROOT" if i % 10 == 0 else f"deps: M{i - 1}", f"module {i}", ""]
        self.plan("\n".join(lines))
        import time as _t
        t0 = _t.time()
        self.assertIn("OK 1001 tasks", self.run_hive("validate").stdout)
        self.run_hive("dispatch")
        self.run_hive("status")
        self.assertLess(_t.time() - t0, 10)


if __name__ == "__main__":
    unittest.main()
