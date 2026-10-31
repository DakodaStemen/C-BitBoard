#!/usr/bin/env bash
# usage: bench.sh <binary> [depth]
# Verifies the deterministic bench node count against tests/bench_nodes.txt
# (format: "<depth> <nodes>"). A mismatch means search/eval behaviour changed:
# if that is intended, regenerate with:  ./cbitboard bench | grep signature
set -eu
BIN=${1:-./cbitboard}
DIR=$(cd "$(dirname "$0")" && pwd)
read -r exp_depth exp_nodes < "$DIR/bench_nodes.txt"
depth=${2:-$exp_depth}
out=$("$BIN" bench "$depth")
echo "$out" | tail -4
got=$(sed -n 's/^Bench signature: depth [0-9]* nodes //p' <<<"$out")
if [[ "$depth" != "$exp_depth" ]]; then echo "depth $depth != reference depth $exp_depth; signature not checked"; exit 0; fi
if [[ "$got" == "$exp_nodes" ]]; then echo "bench signature OK ($got)"; else echo "bench signature MISMATCH: expected $exp_nodes got $got"; exit 1; fi
