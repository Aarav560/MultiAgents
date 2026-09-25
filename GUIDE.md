# hivemind: the guide

hivemind lets one Claude session build big things quickly by running many agents at the same time.
You describe a goal, and Claude writes a plan: a shared brief plus a task graph where every task
owns its own files. A small Python script schedules the tasks, and many subagents build them in
parallel. Each worker reads its instructions from disk and reports back in one line, so a
40-agent run costs close to what one agent doing the same work would.

What it has built (all in this repo, all planned and built by hive):

| example | what it is | agents |
|---|---|---|
| [`examples/starsim`](examples/starsim) | windowed space simulator in pure C (Windows, Linux, macOS) | 14 tasks, 12 in parallel |
| [`demos/spacesim`](demos/spacesim) | `orbit`, a command-line N-body simulator in C with 15 test suites | 27 tasks, 22 in parallel |
| [`examples/saas-api`](examples/saas-api) | a 40-file TypeScript API plan (plan only) | 16 tasks |

---

## 1. Install

Pick one. You need Claude Code, `git` and Python 3.8+ (on Windows Python is usually `python`,
not `python3`; hive handles that).

### A. Open this repo: auto-install

This repository commits [`.claude/settings.json`](.claude/settings.json), which registers the
marketplace and enables the plugin. Clone it, start Claude Code inside it, accept the **trust this
folder** prompt, and Claude Code offers to install `hivemind`. Accept, and it's installed.

```bash
git clone https://github.com/aarav560/multiagents.git
cd multiagents
claude
```

### B. Any project: install from the marketplace

In your terminal:

```bash
claude plugin marketplace add aarav560/multiagents
claude plugin install hivemind@multiagents
```

Or inside a session: `/plugin marketplace add aarav560/multiagents`, then
`/plugin install hivemind@multiagents`. Restart the session (or run `/reload-plugins`).
To install from a branch, add `#<branch>`: `claude plugin marketplace add "aarav560/multiagents#my-branch"`.

### C. Make your own repo auto-install hive for your whole team

From your project's folder:

```bash
claude plugin marketplace add aarav560/multiagents --scope project
claude plugin install hivemind@multiagents --scope project
git add .claude/settings.json && git commit -m "Use hivemind"
```

Everyone who opens the project in Claude Code and trusts the folder is offered hive automatically.

### D. Claude Cowork and claude.ai: upload the package

Cowork and claude.ai use the skills and plugins on your Claude account, not files on your disk.
This repo ships ready-to-upload packages in [`dist/`](dist):

| file | what it contains | use it for |
|---|---|---|
| `dist/hivemind-plugin.zip` | the whole plugin: hive skill, 7 agents, commands, hook | Cowork plugin upload |
| `dist/hive-skill.zip` | only the hive skill (`hive/SKILL.md` + its script and references) | skill upload in Cowork / claude.ai |

- **Plugin (recommended for Cowork):** in the Claude desktop app, open **Cowork**, then
  **Customize → Plugins**, choose to add or upload a plugin, and pick `hivemind-plugin.zip`.
- **Skill only:** in Claude, open **Settings → Capabilities → Skills**, choose **Upload skill**, and pick
  `hive-skill.zip`. Make sure the skill is switched on.

Menu names move between app versions. If you don't see these exact labels, look for "plugins"
or "skills" in Cowork's customize area or in Settings. After uploading, start a new Cowork task
and say "use hive to …".

Rebuild the zips after changing the skill with `./scripts/package.sh`.

### E. Without the plugin system

```bash
git clone https://github.com/aarav560/multiagents.git
cd multiagents
./install.sh             # copies the skill, agents and commands into ~/.claude
./install.sh --project   # or into ./.claude of the current project
./install.sh --uninstall
```

### Check that it's installed

```bash
claude plugin details hivemind
```

It should list the skills (`hive`, `hive-swarm`, `hive-unite`, `hive-status`, `hive-resume`) and 7 agents.
In a session, type `/hivemind:` and the commands appear.

---

## 2. Use it

| plugin install | standalone install | what it does |
|---|---|---|
| `/hivemind:hive <goal>` | `/hive <goal>` | plan, dispatch agents in parallel, verify, report |
| `/hivemind:hive --dry <goal>` | `/hive --dry <goal>` | plan and estimate cost only, don't build |
| `/hivemind:hive-swarm <task>` | `/hive-swarm` | one task per file or module, for bulk work (tests for 30 modules, translate 12 pages) |
| `/hivemind:hive-unite <goal>` | `/hive-unite` | many agents on one deliverable: contracts, then builders, then an integrator and a verifier |
| `/hivemind:hive-status` | `/hive-status` | progress of the current run |
| `/hivemind:hive-resume` | `/hive-resume` | continue a run after a restart |

You can also just ask in plain words: "use hive to build …", "swarm this", "use multiple agents".

### A first run

1. **Dry run first**, to see the plan and the cost before anything is built:
   `/hivemind:hive --dry build a REST API for todos with auth, tests and docs`
2. Read `.hive/plan.md` (the tasks) and `.hive/context.md` (the brief every agent reads).
   Edit anything you don't like.
3. **Build:** `/hivemind:hive build a REST API for todos with auth, tests and docs`.
   Claude dispatches the agents, waits, dispatches the next wave, fixes failures and runs the checks.
4. **Check progress at any time** with `/hivemind:hive-status`.

### Flags

| flag | effect |
|---|---|
| `--lean` | cheaper models (haiku/sonnet), 4 agents at a time |
| (none) | balanced: haiku for grunt work, sonnet for code, opus for design and integration, 8 at a time |
| `--max` | stronger models everywhere, 12 at a time |
| `--inherit` | every agent runs on your current model ("your intellect") |
| `--dry` | plan and estimate only |

### Ask well

- **Be concrete about the result:** language, platform, and what "done" means ("it opens a window on Windows",
  "all tests pass", "runs with `make`").
- **Say what must not change:** "don't touch `src/legacy/`", "no new dependencies".
- **Big jobs:** say "use foreach" and "pack small tasks". Hive can expand one task template into
  hundreds of tasks (for example one per file) and let one agent handle several small ones.
- **Want to see before paying?** Use `--dry`, then run the same command without it.

---

## 3. How it works (short)

```
you -> Claude writes .hive/context.md + .hive/plan.md
       hive.py validate   no cycles, no two agents writing the same file at once
       hive.py dispatch   start every ready task (longest chain first), in one message
         agents: hive.py brief <id>  ->  build their own files  ->  hive.py done <id>
       dispatch again as agents finish ... until ALL DONE, then hive.py verify --run
```

- Each agent **owns** its files. Conflicts are rejected before anything runs.
- Agents coordinate through `ask` (a change request for a file they don't own) and `post` (a shared board).
- State is plain files in `.hive/` (one status file per task). Kill everything and resume later.
- Everything is in [`skills/hive/`](skills/hive): [`SKILL.md`](skills/hive/SKILL.md) (what Claude follows),
  [`scripts/hive.py`](skills/hive/scripts/hive.py) (the scheduler), and [`references/`](skills/hive/references)
  (planning, modes, plan format, token economics).

---

## 4. Troubleshooting

| symptom | cause and fix |
|---|---|
| `/hive` is not a command | Plugin commands carry the plugin's name: use `/hivemind:hive`. Only the standalone `install.sh` gives plain `/hive`. After installing, restart or run `/reload-plugins`. |
| `claude plugin marketplace add` installs nothing, or the plugin has no skills | The marketplace is read from the repo's default branch. If the files are on another branch, add `#branch` to the source. |
| `cd: examples/starsim: No such file or directory` | Your terminal isn't at the top of the repo (the prompt shows `~` or a subfolder), or you're on a branch without it. Run `cd "$(git rev-parse --show-toplevel)"`, then check `git branch --show-current`. |
| Windows: `'build.bat' is not recognized` in Git Bash | Git Bash can't run `.bat` directly and you must be in its folder: `cd` there, then `cmd //c build.bat`. |
| Windows: `No C compiler found` | Install gcc with `winget install BrechtSanders.WinLibs.POSIX.UCRT`, close and reopen the terminal, then check `gcc --version`. |
| Windows: `python3` not found | Windows installs Python as `python`. The kernel already prints `python` in commands on Windows. |
| Agents seem stuck, or a session was restarted | `/hivemind:hive-resume` (it requeues unfinished tasks and continues). |
| A task failed | `/hivemind:hive-status` shows why. Claude fixes the spec and retries, or you can ask it to. |
| Cowork doesn't use hive | Check the plugin or skill is switched on in Cowork's customize area, start a *new* task, and ask "use hive to …". |

---

## 5. For maintainers

```bash
python3 -m unittest discover tests      # kernel tests
claude plugin validate .                # plugin and marketplace manifests
./scripts/package.sh                    # rebuild dist/*.zip for Cowork / claude.ai
```
