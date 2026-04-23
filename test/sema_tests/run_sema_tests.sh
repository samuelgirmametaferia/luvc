#!/bin/sh
set -e
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT_DIR/build/luv"
TEST_DIR="$ROOT_DIR/tests/sema_tests"
PASS=0
FAIL=0
for lv in "$TEST_DIR"/*.lv; do
  name=$(basename "$lv" .lv)
  out="$TEST_DIR/out/${name}.log"
  mkdir -p "$TEST_DIR/out"
  "$BIN" "$lv" > "$out" 2>&1
  expect_file="$TEST_DIR/${name}.expect"
  ok=true
  if [ -f "$expect_file" ]; then
    while IFS= read -r line; do
      if [ -z "$line" ]; then continue; fi
      if ! grep -F -q "$line" "$out"; then
        echo "FAIL: $name - expected to find: $line"
        ok=false
        break
      fi
    done < "$expect_file"
  fi
  if $ok; then
    echo "PASS: $name"
    PASS=$((PASS+1))
  else
    echo "TEST OUTPUT:"
    sed -n '1,200p' "$out"
    FAIL=$((FAIL+1))
  fi
done

echo "Passed: $PASS, Failed: $FAIL"
if [ $FAIL -ne 0 ]; then exit 2; fi
exit 0
