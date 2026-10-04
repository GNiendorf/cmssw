#!/bin/bash
# prop lane self-contained unit test: stock reference on synthetic Phase-2-like tracks, then the port on every
# available Alpaka backend (a backend without a device prints a skip line and passes).
set -e
WORK=$(mktemp -d "${TMPDIR:-/tmp}/proptest.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
testMkFitAlpakaPropStockRef "$WORK/stockref.bin" 5000 4242
for b in SerialSync CudaAsync ROCmAsync; do
  if command -v "testMkFitAlpakaProp$b" > /dev/null; then
    "testMkFitAlpakaProp$b" "$WORK/stockref.bin" | tail -n 1
  fi
done
