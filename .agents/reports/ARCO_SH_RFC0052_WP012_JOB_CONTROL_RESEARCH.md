# WP-012 Job Control — POSIX Process-Group and Controlling-Terminal Research

AP-0052-015 requires this research be written down before any implementation starts. This is that
document. It covers only the POSIX mechanics; the ArcoSH-specific design (host primitives, ArcoBASIC
job table, built-ins) is a separate section at the end, kept short on purpose — get the underlying
protocol right first.

## 1. The actors

- **A job** is one pipeline (one or more processes connected by pipes) started from one command
  line. `cat file | grep x &` is one job with two member processes.
- **A process group (pgrp)** is a set of related processes, identified by a pgid (conventionally the
  pid of the group's first/leader process). Every process belongs to exactly one process group at
  all times (`getpgrp()`/`getpgid(pid)`). WP-004/WP-011's own foreground execution already puts
  every job's members into one shared pgrp (`setpgid`, called in both parent and child to close the
  fork race — see `process_execute_foreground`/`process_execute_pipeline`'s own comments); this WP
  reuses that same pgrp-per-job model, it just stops always waiting for it synchronously.
- **A session** is a set of process groups, with at most one being the session's **controlling
  terminal's foreground process group** at any moment. arcosh's own process group is the session's
  original foreground group; every job it starts is a *new*, separate process group within the same
  session — never a member of arcosh's own group, or a signal meant for one job would hit the shell
  and every other job too.
- **The controlling terminal** tracks exactly one **foreground process group** via `tcgetpgrp(fd)`/
  `tcsetpgrp(fd, pgid)` on the terminal's file descriptor. Only processes in that group may read from
  the terminal without being stopped by `SIGTTIN`, or (if the terminal has `TOSTOP` set — not the
  default, and not touched by anything here) write to it without `SIGTTOU`. Every *other* process
  group attached to the same terminal is, by definition, a **background** group.

## 2. The signals

- **`SIGINT`** (Ctrl-C) and **`SIGQUIT`** (Ctrl-\\): sent by the kernel's line discipline to the
  terminal's current *foreground* process group only. A background job never receives these from the
  terminal (it can still be sent one explicitly via `kill`, out of scope here).
- **`SIGTSTP`** (Ctrl-Z): sent by the kernel to the foreground process group; the default
  disposition stops (not kills) every process in that group. This is the "suspend" ArcoSH must
  detect and turn into a job entering the `Stopped` state.
- **`SIGTTIN`**: sent to a *background* process group if one of its members tries to read from the
  controlling terminal. Default action stops the group. ArcoSH does not need to do anything special
  to cause this — it happens automatically the moment a backgrounded job's own child calls `read()`
  on stdin while not in the foreground group. It DOES need to notice the resulting stop (same
  `waitpid(..., WUNTRACED)` mechanism as `SIGTSTP` below) and report the job as `Stopped`, not just
  silently hang.
- **`SIGTTOU`**: sent to a background group that tries to `tcsetpgrp`/write to the terminal (only
  the latter if `TOSTOP` is set, which this design does not set). ArcoSH's OWN foreground-execution
  code already ignores `SIGTTOU` in the *parent* around every `tcsetpgrp` call it makes (see
  `process_execute_foreground`) — required because the shell itself, when it is not currently the
  foreground group (true for a brief window during job hand-off), would otherwise stop itself the
  instant it calls `tcsetpgrp`.
- **`SIGCONT`**: sent explicitly (`kill(-pgid, SIGCONT)`) to resume a stopped group. A process that
  was never stopped simply ignores it (no observable effect) — safe to send unconditionally to a
  job's group whenever `fg`/`bg` targets it, without first checking whether it was actually stopped.
- **`SIGCHLD`**: sent to the parent (arcosh) whenever any direct child changes state — exits,
  is killed by a signal, stops, or is continued (the latter two only if the parent opted in via
  `WUNTRACED`/`WCONTINUED` on some earlier `wait` call, which this design always does). This is how
  a shell discovers a *background* job finished or stopped without polling — either an installed
  `SIGCHLD` handler, or (the approach here, see Section 4) explicit non-blocking `waitpid(..., pid,
  WNOHANG | WUNTRACED | WCONTINUED)` polls run at safe points (right before printing the next
  prompt, and inside `jobs`/`fg`/`bg` themselves) rather than an async-signal-context handler mutating
  shared state.

## 3. The shell's own required signal posture

For the *entire* time ArcoSH is interactively running (not just around one foreground `wait`, the
way WP-004/WP-011's existing foreground-only code scopes it):

- `SIGINT`, `SIGQUIT`, `SIGTSTP`, `SIGTTIN`, `SIGTTOU` are **ignored** by the shell process itself.
  The shell must never be interrupted/stopped by Ctrl-C/Ctrl-Z typed while a job (foreground or
  background) is running, or while it is sitting at its own prompt — a real interactive shell (bash,
  zsh, dash with job control on) does exactly this, unconditionally, for its own top-level process,
  from startup.
- Every child, immediately after `fork()` and before `exec`, resets all five back to `SIG_DFL` —
  already done correctly by both `process_execute_foreground` and `process_execute_pipeline`; no
  change needed there, a background job's children need the identical reset.
- `SIGCHLD` is left at its default (`SIG_DFL`, i.e. ignored-for-purposes-of-reaping only in the
  sense that the shell must still call `wait`/`waitpid` itself — `SIG_DFL` for `SIGCHLD` does NOT
  auto-reap; only explicitly setting `SIGCHLD` to `SIG_IGN` on some systems does, and this design
  never does that, since it needs the exit status).

## 4. The exact protocol per job-control action

Chapter 9 of Stevens & Rago's *Advanced Programming in the UNIX Environment* is the canonical
reference for all of this; the summary below matches it.

**Starting a job (foreground or background), first process:**
1. `fork()`.
2. Child: `setpgid(0, 0)` (becomes its own group leader, pgid == its own pid).
3. Parent: `setpgid(child_pid, child_pid)` too — the same double-call-in-both-places pattern already
   used to close the fork race, unchanged from the existing single-command/pipeline code.
4. For every subsequent process in the same pipeline: child does `setpgid(0, pgid)` (the FIRST
   child's pid, captured from step 2/3), parent does `setpgid(pid, pgid)` too. Exactly what
   `process_execute_pipeline` already does today.

**Foreground-only, additionally:**
5. Parent: `tcsetpgrp(STDIN_FILENO, pgid)` — hand the terminal to the new group. Must happen AFTER
   step 3/4's `setpgid` calls (a group cannot own the terminal before it exists) and with `SIGTTOU`
   ignored around it (the shell itself may transiently not be the foreground group here).
6. Parent waits with `waitpid(-pgid, &status, WUNTRACED)` — `WUNTRACED` is what turns a `SIGTSTP`-
   or `SIGTTIN`-caused stop into a `waitpid` return (`WIFSTOPPED(status)`) instead of leaving the
   parent blocked until the job *fully exits*, which would make Ctrl-Z during a foreground job do
   nothing observable at the shell (the classic "job control isn't real" bug this AP explicitly
   calls out: "Background execution MUST NOT be faked").
7. On `WIFSTOPPED`: reclaim the terminal (`tcsetpgrp` back to the shell's own pgrp), record the job
   as `Stopped` in the job table (with its current pgid/pids/command text), print the usual
   `[N]+  Stopped                 <command>` line, and return control to the prompt loop —
   critically, WITHOUT reaping the process (it is still alive, just stopped; reaping happens only
   on real exit).
8. On normal exit/signal-death (`WIFEXITED`/`WIFSIGNALED`): reclaim the terminal the same way, but
   now the job really is done — remove/mark it `Done` in the table, return its exit status.

**Backgrounding (`command &`, or `bg %N` on an already-stopped job):**
- Same `fork`/`setpgid` sequence, but skip step 5 entirely (never call `tcsetpgrp` for it — the
  shell's OWN pgrp stays the foreground group the whole time) and skip the blocking `waitpid` in
  step 6 — record the job as `Running` in the table immediately and return to the prompt without
  waiting. `bg %N` on a `Stopped` job additionally sends `SIGCONT` to its pgrp
  (`kill(-pgid, SIGCONT)`) before marking it `Running`; a fresh `command &` needs no `SIGCONT` since
  its children were never stopped.

**Foregrounding (`fg %N`):**
- `tcsetpgrp(STDIN_FILENO, job_pgid)` (with `SIGTTOU` ignored around it, same as any other
  `tcsetpgrp`), then `kill(-job_pgid, SIGCONT)` unconditionally (harmless no-op if it was already
  running), then the SAME blocking `waitpid(-job_pgid, &status, WUNTRACED)` / stop-or-exit handling
  as steps 6-8 above.

**Discovering an *unrequested* state change in a background job** (it finished, or it stopped
itself — e.g. it tried to read stdin and got `SIGTTIN`'d — while the shell was doing something
else): a non-blocking reap loop, `while (waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED) >
0) { ... }`, matching each returned pid back to its job table entry and updating that job's state
(`Done`/`Stopped`/`Running` again on `WIFCONTINUED`). Run this at the start of every prompt line (so
`[N]+  Done  <command>` appears "as soon as reasonably possible" the way real shells document it,
never mid-line) and again inside `jobs` itself so its own output is always current.

**Terminal ownership on shell exit:** if the shell exits (or is killed) while background jobs are
still alive, those jobs are NOT killed by this design (matching real shell default behavior without
`huponexit`) — they simply become orphaned, re-parented to init/subreaper, and keep running or get
`SIGHUP`'d only if the *session* itself is torn down (a controlling-terminal close), which is a
kernel behavior this design does not need to implement or fight.

## 5. What "do not fake it" rules out

Concretely, given the above:

- Spawning a background job with `posix_spawn`/`fork`+`exec` and simply never calling `wait` on it
  (leaving it fully detached, no pgrp of its own, no job-table entry) is the exact anti-pattern the
  AP names. It would run, but `jobs`/`fg`/`bg`/Ctrl-Z would all be no-ops or lies.
- A correct implementation needs, at minimum: per-job process groups (not just per-job pids),
  `WUNTRACED` on every foreground wait (not a plain blocking wait with no stop detection),
  `tcsetpgrp` hand-off in both directions, and a reap-and-update pass covering `WNOHANG` too (for
  background jobs finishing on their own schedule, not just when explicitly `fg`'d).

## 6. Where this leaves the ArcoSH-specific design (implementation, not research)

This document deliberately stops at "what POSIX requires"; the concrete host primitives
(`Process.*`), the ArcoBASIC-level job table, and the `jobs`/`fg`/`bg` built-ins are designed and
implemented in the WP-012 pass itself (see `arcosh/README.md`'s own WP-012 section once written, and
`src/runtime/runtime.cpp`'s job-control primitives for the concrete mechanism each protocol step
above maps onto).
