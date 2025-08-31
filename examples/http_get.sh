#!/usr/bin/env bash
# MIT License
set -euo pipefail

: "${HOST:=example.org}"
: "${PORT:=80}"

if ! type -t socket >/dev/null; then
  enable -f ./build/socket.so socket
fi

socket connect -T 3000 "$HOST" "$PORT" fd
req=$'GET / HTTP/1.0\r\nHost: '"$HOST"$'\r\nUser-Agent: bash-socket/0.1\r\n\r\n'
socket send "$fd" "$req"

socket recv -T 3000 -mode bytes -max 8192 "$fd" chunk || true
printf '%s' "$chunk" | awk 'BEGIN{RS="\r?\n"} NR<=11{print}'
socket close "$fd" || true
