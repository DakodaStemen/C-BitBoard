#!/usr/bin/env bash
# usage: perft_suite.sh <binary> [quick|full]
# quick = all `quick` rows; full = quick + `full` rows. Prints a markdown table.
set -u
BIN=${1:-./cbitboard}; TIER=${2:-quick}
DIR=$(cd "$(dirname "$0")" && pwd)
fail=0
echo "| Position | Depth | Expected | Got | Mnodes/s | Result |"
echo "|---|---|---|---|---|---|"
while IFS=$'\t' read -r name fen depth expected tier; do
  [[ "$name" == \#* || -z "$name" ]] && continue
  [[ "$TIER" == quick && "$tier" != quick ]] && continue
  out=$("$BIN" "$fen" "$depth")
  got=$(sed -n 's/^Nodes: *//p' <<<"$out")
  speed=$(sed -n 's/.*(\([0-9.]*\) Mnodes\/s).*/\1/p' <<<"$out")
  if [[ "$got" == "$expected" ]]; then res="✅ pass"; else res="❌ FAIL"; fail=1; fi
  echo "| $name | $depth | $expected | $got | ${speed:-n/a} | $res |"
done < "$DIR/perft.tsv"
exit $fail
