#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Observe existing parent policy before forking; never set scheduling parameters.
set -u
if [[ $# -lt 2 || ! $1 =~ ^-?[0-9]+$ ]]; then
  echo "usage: bash wait_parent.sh EXPECTED_NICE COMMAND [ARG...]" >&2
  exit 2
fi
expected=$1
shift
deadline=$((SECONDS + 5))
while :; do
  actual=$(ps -o ni= -p $$ | tr -d ' ')
  [[ $actual == "$expected" ]] && break
  if (( SECONDS >= deadline )); then
    echo "parent nice $actual differs from registered $expected; no child started" >&2
    exit 1
  fi
  sleep 0.02
done
echo "parent policy observed: pid=$$ nice=$actual (unchanged)" >&2
# Keep this shell alive: the command inherits the already-observed policy.
"$@"
result=$?
exit "$result"
