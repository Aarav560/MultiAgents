---
description: Resume an interrupted hive run after a restart.
argument-hint: ""
---

Load the `hive` skill via the Skill tool if it is not already loaded.

If `.hive/plan.md` does not exist, tell the user there is no plan to resume and stop.

1. Run `python3 .hive/bin/hive.py status --full` to see what state the run was left in.
2. Run `python3 .hive/bin/hive.py dispatch --requeue`. This is safe: after a restart no agent
   process is still in flight, so any task marked `running` is actually dead and can be safely
   returned to `pending` and redispatched. Tell the user briefly that this is what happened and why
   it's safe.
3. Continue the normal dispatch loop from the skill's protocol: send the printed Agent calls, run
   `dispatch` again as workers finish, repeat until it prints `ALL DONE`.
4. Steer as usual — check `board`/`status` between waves for `ask` requests and failures.
5. Once done, run `verify --run` and report the outcome to the user.
