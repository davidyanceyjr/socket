# socket (Bash loadable builtin)

Minimal TCP sockets for Bash scripts without forking `nc`/`socat`.

## Build & enable

```bash
# Install bash headers (distro package often 'bash-dev' or 'bash-devel').
make                      # -> build/socket.so (release, -O2 -DNDEBUG)
make debug                # -> build/socket.debug.so (-O0 -g3 -DDEBUG)

# Enable (release)
bash -lc 'enable -f ./build/socket.so socket && type -a socket'

# Or enable debug build
bash -lc 'enable -f ./build/socket.debug.so socket && type -a socket'
```

If your headers are not in `/usr/include/bash`, point Makefile at them:

```bash
make BASH_INC=/path/to/bash/include
```

## Usage

```
socket connect [-4|-6] [-n] [-T ms] <host> <port> <varfd>
socket send    [-b64] <fd> [--] <data...>
socket recv    [-T ms] [-max N] [-mode line|bytes|all] <fd> <var>
socket close   <fd>
socket listen  [-b backlog] [-a addr] [-p port] <varfd>
socket accept  [-T ms] <listenfd> <varfd> [<varpeer>]
```

**Exit codes**: 0 success, 1 operational error, 2 usage error, 124 timeout.  
**No stdout noise**. Results go to variables; diagnostics to stderr.

### Examples

HTTP/1.0 GET:
```bash
bash -lc 'enable -f ./build/socket.so socket; examples/http_get.sh'
```

Echo server (single client):
```bash
bash -lc 'enable -f ./build/socket.so socket; examples/echo_server.sh 12345'
```

## Roadmap (v0.2+)
TLS, UDP/UNIX sockets, `socket wait` for multi-FD polling, binary-safe recv-to-fd.
