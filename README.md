# socket (Bash loadable builtin)

Minimal TCP sockets for Bash scripts without forking `nc`/`socat`.

> **Status:** Early MVP. Implements `connect`, `send`, `recv`, `close`, `listen`, `accept` for IPv4/IPv6 TCP with basic timeouts and nonblocking support. TLS/UDP/UNIX sockets and multi-FD polling are not implemented yet.

## Why this exists

When you only need a tiny bit of networking from a Bash script, spawning `nc`/`socat` adds process overhead, complicates timeouts, and makes error handling brittle. This project provides a minimal, compiled Bash **builtin** named `socket` so your script talks TCP directly—no extra processes, clearer behavior, and fast loops.

## Features (current)

- **Subcommands:** `connect`, `send`, `recv`, `close`, `listen`, `accept`
- **IPv4/IPv6** support (force with `-4`/`-6` on `connect`)
- **Timeouts** (`-T <ms>`) and **nonblocking** connect (`-n`)
- **Simple binary support** for `send` via `-b64` (args treated as base64-encoded payload)
- **Receive modes:** line (`-mode line`), fixed byte count (`-mode bytes -max N`), or read-all (`-mode all`).
- **Binary caveat:** Bash variables cannot store NUL bytes. Receiving arbitrary binary into a variable is unsafe; prefer `-b64` for `send` today and track `recv --to-fd` on the roadmap.
- **Exit codes:** `0=OK`, `1=FAIL`, `2=USAGE`, `124=TIMEOUT`
- **Zero stdout noise:** builtin results go to shell variables; errors go to stderr via `builtin_error`. Examples below print via `printf`/`echo`, not the builtin itself.
- **Examples & tests:** See `examples/` and `tests/mvp.bats`

> Derived from the codebase in `src/socket_builtin.c`, helper headers in `include/`, examples in `examples/`, and a basic test in `tests/`.

## Project goals

- Provide a **tiny, dependency-free** network primitive for Bash scripts
- Keep the **API minimal and shell-friendly**
- Deliver **predictable timeouts** and **clear error codes**
- Avoid external processes and complex pipelines for simple TCP use cases

## Build

Requires Bash development headers. Package names vary by distro:

- Fedora/RHEL: `bash-devel`
- Alpine: `bash-dev`
- Debian/Ubuntu: headers are typically bundled with `bash`; you may need the source package or a newer Bash.

```bash
# Release build (-O2 -DNDEBUG)
make                       # -> build/socket.so

# Debug build (-O0 -g3 -DDEBUG)
make debug                 # -> build/socket.debug.so

# Load the builtin into a new Bash:
bash -lc 'enable -f ./build/socket.so socket && type -a socket'

# Or load the debug build:
bash -lc 'enable -f ./build/socket.debug.so socket && type -a socket'
```

> The Makefile looks for headers via `BASH_INC` (default `/usr/include/bash` on some distros). On Homebrew/macOS this is typically `/opt/homebrew/opt/bash/include/bash`. Set `BASH_INC`/`EXTRA_INC` accordingly.

## Quick start

HTTP/1.0 GET (from `examples/http_get.sh`):

```bash
bash -lc 'enable -f ./build/socket.so socket; examples/http_get.sh'
```

Single-client echo server (from `examples/echo_server.sh`):

```bash
bash -lc 'enable -f ./build/socket.so socket; examples/echo_server.sh 12345'
```

## CLI reference

```
usage:
  socket connect [-4|-6] [-n] [-T ms] <host> <port> <varfd>
  socket send    [-b64] <fd> [--] <data...>
  socket recv    [-T ms] [-max N] [-mode line|bytes|all] <fd> <var>
                 # with -mode bytes, -max N is required
  socket close   <fd>
  socket listen  [-b backlog] [-a addr] [-p port] <varfd>
  socket accept  [-T ms] <listenfd> <varfd> [<varpeer>]
```

### `connect`
Create a TCP connection and place the resulting file descriptor into `<varfd>`.

- `-4` / `-6` — force IPv4 / IPv6
- `-n` — nonblocking connect
- `-T ms` — connect timeout in milliseconds (`-1` means infinite, `0` immediate check)
  
  *Note:* this timeout applies to the TCP connect phase; DNS resolution can block separately unless handled asynchronously.

### `send`
Send data to an open socket `<fd>`.

- `-b64` — treat `data...` as **base64-encoded** payload before sending (handy for binary)
- `--` — end of options (useful if data begins with `-`)

### `recv`
Read from socket `<fd>` into shell variable `<var>`.

- `-T ms` — recv timeout per wait (`-1` infinite, `0` poll without wait)
- `-max N` — cap bytes to read (required for `-mode bytes`, optional otherwise)
- `-mode line|bytes|all`
  - `line`: read up to and including `\n` (the newline is included in `<var>`)
  - `bytes`: read exactly `N` bytes (set with `-max`)
  - `all`: read until EOF or `-max`

### `close`
Close an open socket `<fd>`. Returns `1` if it was already closed/invalid.

### `listen`
Create a listening socket and place the fd into `<varfd>`.

- `-a addr` — bind address (e.g., `0.0.0.0`, `::`)
- `-p port` — numeric port
- `-b backlog` — listen backlog (system default if omitted)

### `accept`
Accept a client on `<listenfd>`, place the connected fd into `<varfd>`, and optionally the peer string into `<varpeer>`.

- `-T ms` — accept timeout (`-1` infinite, `0` nonblocking)

## Examples

### Minimal client
```bash
enable -f ./build/socket.so socket
socket connect -T 3000 example.org 80 fd
req=$'GET / HTTP/1.0\r\nHost: example.org\r\n\r\n'
socket send "$fd" "$req"
socket recv -T 3000 -mode bytes -max 4096 "$fd" chunk || true  # demo cap: replies >4096 bytes will be truncated
printf '%s' "$chunk"
socket close "$fd" || true
```

### Minimal server (single client)
```bash
enable -f ./build/socket.so socket
socket listen -a 0.0.0.0 -p 12345 lfd
socket accept -T -1 "$lfd" cfd peer
echo "client: $peer" >&2
while socket recv -T 60000 -mode line "$cfd" line; do  # add a sane timeout for demos
  [[ -z "$line" ]] && break
  socket send "$cfd" "$line" || break
done
socket close "$cfd"; socket close "$lfd"
```

## Error handling

- Success: `0`
- Failure: `1`
- Usage error: `2`
- **Timeout:** `124` (deliberately matches common `timeout(1)` conventions)

Errors are printed via Bash’s `builtin_error` to stderr. Data is returned via variables, not stdout.

## Repository layout

```
Makefile
include/           # small helpers (arg parsing, util incl. base64 helpers)
src/socket_builtin.c
examples/          # http_get.sh, echo_server.sh
tests/mvp.bats     # basic happy-path + timeout tests
build/             # .so outputs when built (release/debug)
```

## Current limitations / known gaps

- **TLS**, **UDP**, and **UNIX domain sockets** are not implemented
- No built-in **multi-FD wait/poll** (`socket wait`) helper yet
- `recv` stores into a shell variable (copy in memory). NUL bytes are not storable in shell vars; no zero-copy `recv -> fd`
- Limited test coverage (single-file Bats harness)
- Packaging (deb/rpm/apk/homebrew) and CI pipelines are not set up here


## Compatibility
Tested with modern Bash (≥4.x). macOS system Bash 3.2 is too old; install a newer Bash (e.g., Homebrew) before loading the builtin (`enable -f ...`).

## Networking notes
- Binding to ports <1024 requires privileges on Unix-like systems.
- On Linux, an IPv6 bind to `::` may or may not accept IPv4 depending on `net.ipv6.bindv6only` (dual-stack behavior is system-configurable).

## Roadmap to a production-ready v1.0

### v0.2 – API polish & I/O completeness
- Add `socket wait` (poll multiple fds; expose readiness masks)
- Add `recv --to-fd <n>` and `send --from-fd <n>` for **binary-safe** streaming
- Add `socket shutdown <fd> [read|write|both]`
- Expose `peername`/`sockname` queries (`socket peer <fd> var`, `socket local <fd> var`)
- Tighten argument validation & consistent error messages
- Expand examples and man-style help (`socket --help` prints usage)

### v0.3 – Protocol/transport coverage
- **TLS client** support (OpenSSL or mbedTLS; ALPN + SNI)
- **UNIX domain sockets** (`-a /path`)
- **UDP** (`socket udp-send`, `socket udp-recv` or `socket open -u` mode)
- Optional `-keepalive`, `-nodelay` toggles on TCP

### v0.4 – Reliability & portability
- Comprehensive Bats tests (IPv4/IPv6, timeouts, partials, edge cases)
- Fuzz simple parsers (AFL/libFuzzer harness around base64 and line reader)
- Static analysis (clang-tidy, -fsanitize=address/undefined in CI)
- Cross-distro builds and CI matrix (Ubuntu, Debian, Alpine, Fedora)
- macOS build via Homebrew Bash headers

### v0.5 – Packaging & docs
- Ship **man page** (`socket(1)`), extended README, and API reference
- Packages: **deb**, **rpm**, **apk**, **Homebrew tap**
- Versioned semantic releases, CHANGELOG, signed tags

### v1.0 – GA criteria
- API **frozen and documented**
- CI **green** across Linux/macOS; reproducible builds
- **TLS**, **UNIX**, **wait**, and **streaming to/from fd** delivered
- Robust tests for timeouts, nonblocking paths, and error codes
- Backwards-compatible from 1.0 onward

## Contributing

Issues and PRs are welcome. Please run `make test` locally (uses Bats when available) and include new tests for behavior changes. Keep the API minimal and shell-friendly.

## License

MIT — see headers and source file prologues.
