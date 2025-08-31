#!/usr/bin/env bash
# MIT License
set -euo pipefail

PORT="${1:-12345}"

if ! type -t socket >/dev/null; then
  enable -f ./build/socket.so socket
fi

echo "Listening on 0.0.0.0:${PORT} (single client, Ctrl-C to exit)" >&2
socket listen -a 0.0.0.0 -p "$PORT" lfd
socket accept -T -1 "$lfd" cfd peer
echo "Client: $peer" >&2

while true; do
  if socket recv -mode line "$cfd" line; then
    [[ -z "$line" ]] && break
    socket send "$cfd" "$line" || break
  else
    code=$?
    if [[ $code -eq 124 ]]; then continue; else break; fi
  fi
done

socket close "$cfd" || true
socket close "$lfd" || true
echo "Bye." >&2
