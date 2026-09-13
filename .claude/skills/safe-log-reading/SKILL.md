---
name: safe-log-reading
description: Guidelines and best practices for safely reading and inspecting large log files without loading them entirely into context.
---

Use this skill before opening, reading, `cat`-ing, `tail`-ing, or "checking" any log file (`.log`, `.txt` logs, syslog, journalctl output, CI/CD logs, application/server logs, crash dumps) — especially before dumping a whole file into context. Also trigger on "check the logs", "find errors in the log", "what happened around a given time", "summarize this log", "debug from these logs", or any reference to a log file without exact lines specified.

## Why

Log files are often huge and mostly noise (health checks, debug spam, routine INFO lines). A "small" file can still be tens of thousands of lines. Loading it all wastes tokens, can blow past context limits, and buries the signal (errors, the incident window, a specific request ID) the user actually needs. Answer the question with the smallest slice of the file necessary.

**The rule:** never read/cat an entire log file, even if it "fits" — check size first, then use a targeted command.

## Step 0: Know your shell

Don't assume bash. The shell depends on the user's OS (bash/zsh/WSL/Git Bash vs. PowerShell/cmd on Windows) — commands from one will error in the other. Check if unclear, then use the matching commands below.

## Step 1: Check size and shape first

bash / zsh / WSL / Git Bash:
```bash
wc -l <file>          # line count
du -h <file>          # file size
head -n 3 <file>      # format check (timestamp/level style)
tail -n 3 <file>      # latest format, still being written?
```

PowerShell:
```powershell
(Get-Item <file>).Length              # file size in bytes
Get-Content <file> -TotalCount 3      # first 3 lines
Get-Content <file> -Tail 3            # last 3 lines
(Get-Content <file> | Measure-Object -Line).Lines   # line count (slow on huge files; skip if size alone answers it)
```

Treat anything over ~500 lines or ~100KB as "large" and never view it in full. A genuinely small file is fine to view directly — that's what this check tells you.

## Step 2: Pick the narrowest tool for the question

| Question | bash / zsh | PowerShell |
|---|---|---|
| What errors happened? | `grep -n -iE 'error\|exception\|fatal\|panic' file` | `Select-String -Path file -Pattern 'error\|exception\|fatal\|panic'` |
| Show me around that error | `grep -n -A5 -B5 match file` | `Select-String -Path file -Pattern match -Context 5,5` |
| What's happening recently? | `tail -n 100 file` (or `tail -f` with a timeout) | `Get-Content file -Tail 100` (or `-Wait` with a timeout) |
| What happened around 14:32? | `awk`/`sed -n` on the timestamp | `Select-String -Path file -Pattern '14:3[0-2]'` |
| How many times did X happen? | `grep -c pattern file` | `(Select-String -Path file -Pattern pattern).Count` |
| Overview / summary | `bash scripts/log_probe.sh file` | `powershell -File scripts/log_probe.ps1 -Path file` |
| Find request/trace ID abc123 | `grep -n abc123 file` | `Select-String -Path file -Pattern abc123` |
| Known line range | `sed -n range print` | `Get-Content file \| Select-Object -Skip N -First M` |

If none of these fit and you genuinely need broad context, read in bounded chunks (500-1000 lines) and summarize each before moving to the next — never pull it all in at once.

## Step 3: Use the probe script for open-ended requests

For "check the logs" / "what's going on" style requests where you don't yet know what you're looking for:

```bash
bash scripts/log_probe.sh <file>
```
```powershell
powershell -File scripts/log_probe.ps1 -Path <file>
```

Both report line count, file size, detected time range, per-level counts (ERROR/WARN/INFO/DEBUG, best-effort), and the top recurring error/exception messages — without dumping the file. Treat the output as step one: use it to decide what to search for next, not as the final answer.

## Step 4: Report findings, not raw dumps

Quote only the specific lines that matter. Summarize repeated matches ("47 occurrences of a connection timeout to db-primary between 14:30-14:45") instead of pasting all 47 lines.

## Edge cases

- **Compressed** (`.gz`, `.bz2`): `zgrep`/`zcat | head` on bash; decompress only a small preview on PowerShell (e.g. `7z`/`gzip` if installed) — never fully extract just to peek.
- **Rotated** (`app.log`, `app.log.1`, `app.log.2.gz`, ...): check sizes across all of them first before deciding where to look.
- **Structured/JSON**: `jq 'select(.level=="ERROR")' file` on bash; on PowerShell, parse per-line or on a bounded slice — piping a huge file through `ConvertFrom-Json` still means reading all of it.
- **Live/streaming**: `tail -f` or `Get-Content -Wait`, always with a timeout — never unbounded.
