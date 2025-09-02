#!/usr/bin/env bash

# -------------------------------------------------------------------
# Dual-mode test file:
# - Under Bats: defines tests with @test (uses run_socket helper).
# - As plain bash: runs a small harness and exits before @test blocks.
# -------------------------------------------------------------------

# Helper for Bats: run a fresh bash -lc with the builtin preloaded.
# Usage: run_socket 'commands...'
run_socket() {
  run bash -lc "enable -f ./build/socket.so socket; $1"
}

# -----------------------------
# Plain bash harness (no Bats)
# -----------------------------
if [[ -z "${BATS_VERSION:-}" ]]; then
  set -euo pipefail
  echo "Running plain bash harness (BATS not detected)"
  enable -f ./build/socket.so socket || { echo 'enable failed'; exit 1; }

  echo "=== echo roundtrip"
  socket listen -a 127.0.0.1 -p 12345 l
  (
    set -e
    socket accept -T 2000 "$l" s peer
    socket recv  -T 2000 -mode line "$s" line
    socket send  "$s" "$line"
    socket close "$s" || true
  ) & spid=$!
  sleep 0.1
  socket connect -T 2000 127.0.0.1 12345 c
  socket send "$c" "hello"$'\n'
  socket recv -T 2000 -mode line "$c" out
  if [[ "$out" == "hello" || "$out" == "hello"$'\n' ]]; then
    echo "ok - echo roundtrip"
  else
    printf "not ok - echo roundtrip (got [%q])\n" "$out" >&2; exit 1
  fi
  wait "$spid" || true

  echo "=== recv timeout no data"
  socket listen -a 127.0.0.1 -p 12346 l2
  ( socket accept -T 1000 "$l2" s2 peer || exit 1; sleep 1; socket close "$s2" || true ) &
  sleep 0.1
  socket connect -T 1000 127.0.0.1 12346 c2
  if socket recv -T 100 -mode line "$c2" out2; then
    echo "not ok - expected timeout"; exit 1
  else
    test $? -eq 124 && echo "ok - timeout"
  fi

  echo "=== double close returns 1"
  socket connect -T 2000 127.0.0.1 12345 c3 || socket connect -T 2000 127.0.0.1 12346 c3
  socket close "$c3" || true
  if socket close "$c3"; then
    echo "not ok - second close should fail"; exit 1
  else
    test $? -eq 1 && echo "ok - double close"
  fi

  exit 0
fi

# --------------------
# Bats tests below
# --------------------

# Also load the builtin in the parent Bats shell (some tests call it directly).
setup() {
  enable -f ./build/socket.so socket
}

@test "echo roundtrip (builtin listener)" {
  run_socket '
    set -e
    socket listen -a 127.0.0.1 -p 12345 l
    (
      set -e
      socket accept -T 2000 "$l" s peer
      socket recv  -T 2000 -mode line "$s" line
      socket send  "$s" "$line"
      socket close "$s" || true
    ) & spid=$!
    sleep 0.1
    socket connect -T 2000 127.0.0.1 12345 c
    socket send "$c" "hello"$'\''\n'\''
    socket recv -T 2000 -mode line "$c" out || exit 1
    if [[ "$out" == "hello" || "$out" == "hello"$'\''\n'\'' ]]; then
      wait "$spid" || true
      exit 0
    else
      printf "unexpected echo: [%q]\n" "$out" >&2
      exit 1
    fi
  '
  [ "$status" -eq 0 ]
}

@test "recv timeout no data (no server write, client read should timeout 124)" {
  run_socket '
    set -e
    socket listen -a 127.0.0.1 -p 12346 l
    ( socket accept -T 1000 "$l" s peer || exit 1; sleep 1; socket close "$s" || true ) &
    sleep 0.1
    socket connect -T 1000 127.0.0.1 12346 c2
    socket recv -T 100 -mode line "$c2" out
  '
  [ "$status" -eq 124 ]
}

@test "double close returns 1" {
  run_socket '
    set -e
    # connect to whichever listener is up; try 12345 first, else 12346
    socket connect -T 500 127.0.0.1 12345 c || socket connect -T 500 127.0.0.1 12346 c
    socket close "$c" || true
    socket close "$c"
  '
  [ "$status" -eq 1 ]
}
