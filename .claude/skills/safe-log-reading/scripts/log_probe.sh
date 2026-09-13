#!/usr/bin/env bash
# log_probe.sh — get a compact overview of a log file WITHOUT loading it fully.
# Usage: bash log_probe.sh <path-to-log-file> [top-n-errors]

set -euo pipefail

FILE="${1:-}"
TOP_N="${2:-10}"

if [[ -z "$FILE" || ! -f "$FILE" ]]; then
  echo "Usage: log_probe.sh <path-to-log-file> [top-n-errors]" >&2
  exit 1
fi

echo "== File =="
echo "Path: $FILE"
du -h "$FILE" | awk '{print "Size: " $1}'
LINE_COUNT=$(wc -l < "$FILE")
echo "Lines: $LINE_COUNT"
echo

echo "== First & last lines (format check) =="
echo "--- head ---"
head -n 3 "$FILE"
echo "--- tail ---"
tail -n 3 "$FILE"
echo

echo "== Approximate time range =="
# Best-effort: pull the first token that looks like a timestamp from first/last lines.
# Not guaranteed to match every log format — treat as a hint, not ground truth.
head -n 1 "$FILE" | grep -oE '[0-9]{4}-[0-9]{2}-[0-9]{2}[ T][0-9:.,]+' | head -n 1 || echo "(no ISO-style timestamp detected on first line)"
tail -n 1 "$FILE" | grep -oE '[0-9]{4}-[0-9]{2}-[0-9]{2}[ T][0-9:.,]+' | head -n 1 || echo "(no ISO-style timestamp detected on last line)"
echo

echo "== Log level counts (best-effort, case-insensitive) =="
for LEVEL in FATAL CRITICAL ERROR EXCEPTION WARN WARNING INFO DEBUG TRACE; do
  COUNT=$(grep -icE "\\b${LEVEL}\\b" "$FILE" || true)
  if [[ "$COUNT" -gt 0 ]]; then
    printf "%-10s %s\n" "$LEVEL" "$COUNT"
  fi
done
echo

echo "== Top ${TOP_N} recurring error/exception lines (normalized, most frequent first) =="
# Strip leading timestamps/line-numbers so similar errors group together, then count.
grep -iE 'error|exception|fatal|panic|traceback' "$FILE" \
  | sed -E 's/[0-9]{4}-[0-9]{2}-[0-9]{2}[ T][0-9:.,]+//g; s/[0-9]+/N/g' \
  | sort | uniq -c | sort -rn | head -n "$TOP_N" || echo "(no error-like lines found)"
echo

echo "== Next steps =="
echo "Use grep/awk/sed with the patterns above to drill into specific lines — avoid viewing the full file."