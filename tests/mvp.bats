#!/usr/bin/env bash
# If Bats is not available, this file also runs as a plain bash harness.
# MIT License

if [[ -z "${BATS_VERSION:-}" ]]; then
  run_case() {
    local name=$1; shift
    echo "=== $name"
    if "$@"; then echo "ok - $name"; else echo "not ok - $name"; fi
  }
  enable -f ./build/socket.so socket || { echo "enable failed"; exit 1; }

  run_case "echo roundtrip" bash -c '
    socket listen -a 127.0.0.1 -p 12345 srv || exit 1
    {
      socket accept -T 1000 "$srv" cfd peer & apid=$!
      sleep 0.05
      socket connect -T 1000 127.0.0.1 12345 c || exit 1
      socket send "$c" "hello"
      socket send "$c" $'\''\n'\''
      wait "$apid" || true
      socket recv -T 1000 -mode line "$c" line || exit 1
      [[ "$line" == "hello"$'\''\n'\'' ]]
    } || exit 1
  '

  run_case "recv timeout no data" bash -c '
    socket listen -a 127.0.0.1 -p 12346 l || exit 1
    ( socket accept -T 200 "$l" c & ) &
    sleep 0.05
    socket connect -T 1000 127.0.0.1 12346 c2 || exit 1
    if socket recv -T 100 -mode line "$c2" out; then
      exit 1
    else
      test $? -eq 124
    fi
  '

  run_case "double close" bash -c '
    socket connect -T 2000 example.org 80 c || exit 1
    socket close "$c" || exit 1
    if socket close "$c"; then exit 1; else test $? -eq 1; fi
  '

  exit 0
fi

setup() {
  enable -f ./build/socket.so socket
}

@test "echo roundtrip (builtin listener)" {
  run bash -lc '
    socket listen -a 127.0.0.1 -p 12345 srv
    ( socket accept -T 1000 "$srv" sfd peer & echo $! > /tmp/acc.pid ) &
    sleep 0.05
    socket connect -T 1000 127.0.0.1 12345 c
    [[ $? -eq 0 ]]
    socket send "$c" "hello"
    socket send "$c" $'\''\n'\''
    socket recv -T 1000 -mode line "$c" line
    [[ "$line" == "hello"$'\''\n'\'' ]]
  '
  [ "$status" -eq 0 ]
}

@test "recv timeout no data" {
  run bash -lc '
    socket listen -a 127.0.0.1 -p 12346 l
    ( socket accept -T 200 "$l" c & ) &
    sleep 0.05
    socket connect -T 1000 127.0.0.1 12346 c2
    socket recv -T 100 -mode line "$c2" out
  '
  [ "$status" -eq 124 ]
}

@test "double close returns 1" {
  run bash -lc '
    socket connect -T 2000 example.org 80 c
    socket close "$c"
    socket close "$c"
  '
  [ "$status" -eq 1 ]
}
