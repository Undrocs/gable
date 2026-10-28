#!/usr/bin/env bash
# Measures the memory a program needs when it creates short-lived strings.
#
# tools/bench/temporaries.gable builds a 4 KiB string, uses its length, and drops
# it, once per iteration. If those blocks were kept, 400000 iterations would need
# 1.6 GiB; the run is required to stay under a fixed, much smaller peak. The
# numbers are the program's own peak resident set size, measured by a small
# helper that the script builds, so the check does not depend on this shell.
#
#   tools/check-memory.sh [compiler]        # default: build/manual/gable
#
# POSIX only (fork, wait4, getrusage); the Linux CI job runs it.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
gable=${1:-"$root/build/manual/gable"}
cc=${CC:-cc}
if [[ ! -x $gable ]]; then
  echo "no compiler at $gable; build one with tools/build-without-cmake.sh" >&2
  exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/gable-memory-XXXXXX")
trap 'rm -rf "$work"' EXIT

# Runs a program with its standard output sent to a file, and prints the peak
# resident set size of that program in kibibytes.
cat > "$work/peak.c" <<'C'
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
  /* peak <output-file> <program> [arguments...] */
  if (argc < 3) {
    fprintf(stderr, "usage: peak <output-file> <program> [arguments...]\n");
    return 2;
  }
  const pid_t child = fork();
  if (child == 0) {
    const int out = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out < 0 || dup2(out, 1) < 0) {
      _exit(126);
    }
    execv(argv[2], &argv[2]);
    _exit(127);
  }
  if (child < 0) {
    return 2;
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    return 2;
  }
  if (!WIFEXITED(status)) {
    fprintf(stderr, "program did not exit normally\n");
    return 3;
  }
  if (WEXITSTATUS(status) != 0) {
    fprintf(stderr, "program exited with status %d\n", WEXITSTATUS(status));
    return 4;
  }
  struct rusage usage;
  if (getrusage(RUSAGE_CHILDREN, &usage) != 0) {
    return 2;
  }
  /* Linux reports ru_maxrss in kibibytes; macOS reports bytes. */
#if defined(__APPLE__)
  printf("%ld\n", usage.ru_maxrss / 1024);
#else
  printf("%ld\n", usage.ru_maxrss);
#endif
  return 0;
}
C
"$cc" -std=c17 -O1 -o "$work/peak" "$work/peak.c"

echo "== building the workload"
"$gable" build "$root/tools/bench/temporaries.gable" -o "$work/temporaries"

limit_kb=16384
fail=0
check_run() {
  local iterations=$1
  local expected=$2
  local output_file="$work/output.txt"
  local peak actual started elapsed
  started=$(date +%s%N)
  peak=$("$work/peak" "$output_file" "$work/temporaries" "$iterations" 2>&1) || {
    echo "FAIL $iterations iterations: the program failed: $peak" >&2
    fail=1
    return
  }
  elapsed=$(( ($(date +%s%N) - started) / 1000000 ))
  actual=$(cat "$output_file")
  if [[ $actual != "$expected" ]]; then
    echo "FAIL $iterations iterations: expected '$expected', got '$actual'" >&2
    fail=1
    return
  fi
  if [[ -z $peak || $peak -gt $limit_kb ]]; then
    echo "FAIL $iterations iterations: peak ${peak:-unknown} KiB exceeds ${limit_kb} KiB" >&2
    fail=1
    return
  fi
  echo "PASS $iterations iterations: ${peak} KiB peak, ${elapsed} ms"
}

# 4096 bytes per iteration: 40000 iterations allocate 160 MiB, and 400000
# allocate 1.6 GiB, if nothing is reclaimed. The limit is where reclamation
# shows: the same binary under a runtime that never collects peaks at 3.2 GiB.
check_run 40000 163840000
check_run 400000 1638400000

if [[ $fail -ne 0 ]]; then
  echo "FAILED memory reclamation" >&2
  exit 1
fi
echo "PASS memory reclamation"
