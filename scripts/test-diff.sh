#!/usr/bin/env bash
# test-diff.sh — Busportal fork, M5 / F4. Run `bin/vroom --diff-before A
# --diff-after B` on every `before-<label>.json` / `after-<label>.json`
# pair under $FIXTURES and assert the output matches `expected-<label>.json`
# key-by-key. Exits non-zero on the first mismatch.

set -euo pipefail

FIXTURES="${1:-tests/fixtures/diff}"
BINARY="${VROOM_BIN:-./bin/vroom}"

if [[ ! -x "$BINARY" ]]; then
  echo "Missing binary: $BINARY" >&2
  exit 1
fi

if [[ ! -d "$FIXTURES" ]]; then
  echo "No fixtures directory: $FIXTURES" >&2
  exit 1
fi

shopt -s nullglob
found=0
fail=0
for before in "$FIXTURES"/before-*.json; do
  label=$(basename "$before" .json | sed 's/^before-//')
  after="$FIXTURES/after-$label.json"
  expected="$FIXTURES/expected-$label.json"
  if [[ ! -f "$after" ]]; then
    echo "skip $label: missing after-$label.json"
    continue
  fi
  if [[ ! -f "$expected" ]]; then
    echo "skip $label: missing expected-$label.json"
    continue
  fi
  found=$((found + 1))

  if ! actual=$("$BINARY" --diff-before "$before" --diff-after "$after" 2>/dev/null); then
    echo "FAIL $label: binary error"
    fail=$((fail + 1))
    continue
  fi

  # Canonical compare via jq -S (sort keys); tolerant of formatting
  # differences between our pretty-print and the expected file.
  if diff <(jq -S . "$expected") <(echo "$actual" | jq -S .) >/dev/null; then
    echo "OK   $label"
  else
    echo "FAIL $label:"
    diff <(jq -S . "$expected") <(echo "$actual" | jq -S .) | head -40 >&2
    fail=$((fail + 1))
  fi
done

if [[ $found -eq 0 ]]; then
  echo "No diff fixture triples found in $FIXTURES" >&2
  exit 2
fi

if [[ $fail -gt 0 ]]; then
  echo "$fail diff regression(s) detected" >&2
  exit 1
fi

echo "all $found diff fixtures OK"
