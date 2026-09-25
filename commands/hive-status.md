---
description: Show a compact summary of the active hive run.
argument-hint: ""
---

Load the `hive` skill via the Skill tool if it is not already loaded.

If `.hive/plan.md` does not exist, tell the user there is no active plan and stop.

Otherwise run:

```
python3 .hive/bin/hive.py status --full
```

Summarize the output for the user in exactly 3 lines:
1. Overall progress (tasks done / running / pending / failed, and current wave if shown).
2. Anything that needs attention: failed tasks, open `ask` requests, or tasks stuck running.
3. The single next action (run dispatch, fix a spec, requeue, or nothing — waiting on running agents).

Don't paste the raw command output; give the summary only.
