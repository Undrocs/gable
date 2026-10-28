#!/usr/bin/env bash
# Times the notes tool on a file with many tasks.
#
# Every mutating command reads the whole file and writes it back, so this number
# shows what a text program pays for its input: a program that concatenates lines
# with `+` copies the file once per task, while one that splits, edits a list, and
# joins copies it once.
#
#   tools/measure-notes.sh [compiler] [tasks]     # default: build/manual/gable, 20000
#
# The numbers are wall-clock milliseconds including process start-up, on the host
# that runs the script; they are comparable between two builds on one host, not
# across machines.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
gable=${1:-"$root/build/manual/gable"}
tasks=${2:-20000}
if [[ ! -x $gable ]]; then
  echo "no compiler at $gable; build one with tools/build-without-cmake.sh" >&2
  exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/gable-notes-XXXXXX")
trap 'rm -rf "$work"' EXIT

echo "== building the notes tool"
"$gable" build "$root/tools/notes/notes.gable" -o "$work/notes" -O2

echo "== writing a file with $tasks tasks"
awk -v count="$tasks" 'BEGIN { for (i = 1; i <= count; ++i) printf "0\ttask number %d\n", i }' \
  > "$work/notes.txt"

run() {
  local label=$1
  shift
  local started elapsed
  started=$(date +%s%N)
  "$work/notes" --file "$work/notes.txt" "$@" > /dev/null
  elapsed=$(( ($(date +%s%N) - started) / 1000000 ))
  echo "  $label: ${elapsed} ms"
}

echo "== timings ($tasks tasks)"
run "list" list
run "done 1 (rewrite)" done 1
run "remove 1 (rewrite)" remove 1
head -1 "$work/notes.txt" | sed 's/^/  first task now: /'
