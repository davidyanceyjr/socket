/* MIT License
 * Bash loadable builtin: socket
 * Minimal TCP sockets without external processes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "builtins.h"
#include "shell.h"
#include "variables.h"

#include "argparse.h"
#include "util.h"

/* Status codes (avoid clashing with bash's EX_* macros) */
#define SB_EX_USAGE   2
#define SB_EX_FAIL    1
#define SB_EX_OK      0
#define SB_EX_TIMEOUT 124

/* Some bash header variants don't declare builtin_error */
extern void builtin_error (const char *fmt, ...) __attribute__((format(printf,1,2)));

/* Forward declarations to satisfy -Wmissing-prototypes */
static int cmd_connect(int argc, char **argv);
static int cmd_send(int argc, char **argv);
static int cmd_recv(int argc, char **argv);
static int cmd_close(int argc, char **argv);
static int cmd_listen(int argc, char **argv);
static int cmd_accept(int argc, char **argv);
static int socket_dispatch(int argc, char **argv);
int socket_builtin(WORD_LIST *list); /* exported entry */

/* usage text */
static void print_usage(void) {
    fprintf(stderr,
"usage:\n"
"  socket connect [-4|-6] [-n] [-T ms] <host> <port> <varfd>\n"
"  socket send    [-b64] <fd> [--] <data...>\n"
"  socket recv    [-T ms] [-max N] [-mode line|bytes|all] <fd> <var>\n"
"  socket close   <fd>\n"
"  socket listen  [-b backlog] [-a addr] [-p port] <varfd>\n"
"  socket accept  [-T ms] <listenfd> <varfd> [<varpeer>]\n");
}

/* Bash helper: set var NAME to VALUE (string). No stdout. */
static int set_sh_var(const char *name, const char *value) {
    SHELL_VAR *v = bind_variable(name, value, 0);
    if (v == NULL) {
        builtin_error("failed to set variable '%s'", name);
        return -1;
    }
    return 0;
}
static int set_sh_var_int(const char *name, int v) {
    char buf[64]; snprintf(buf, sizeof(buf), "%d", v);
    return set_sh_var(name, buf);
}

/* WORD_LIST -> argv */
static int list_to_argv(WORD_LIST *list, char ***argv_out) {
    int n = 0;
    for (WORD_LIST *w=list; w; w=w->next) n++;
    char **av = (char**)calloc((size_t)n+1, sizeof(char*));
    if (!av) return -1;
    int i=0;
    for (WORD_LIST *w=list; w; w=w->next) av[i++] = savestring(w->word->word);
    av[i] = NULL;
    *argv_out = av;
    return n;
}
static void free_argv(int argc, char **argv) {
    for (int i=0;i<argc;i++) free(argv[i]);
    free(argv);
}

/* CONNECT */
static int cmd_connect(int argc, char **argv) {
    int af = AF_UNSPEC;
    int want_nonblock = 0;
    int timeout_ms = -1;
    int i = 0;

    while (i < argc && arg_is_flag(argv[i])) {
        if (strcmp(argv[i], "-4") == 0) { af = AF_INET; i++; continue; }
        if (strcmp(argv[i], "-6") == 0) { af = AF_INET6; i++; continue; }
        if (strcmp(argv[i], "-n") == 0) { want_nonblock = 1; i++; continue; }
        if (strcmp(argv[i], "-T") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            unsigned u; int e = parse_uint(argv[i+1], &u); if (e) { print_usage(); return SB_EX_USAGE; }
            timeout_ms = (int)u; i+=2; continue;
        }
        break;
    }
    if (argc - i != 3) { print_usage(); return SB_EX_USAGE; }
    const char *host = argv[i], *port = argv[i+1], *varfd = argv[i+2];

    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;

    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        builtin_error("connect: getaddrinfo(%s,%s): %s", host, port, gai_strerror(gai));
        return SB_EX_FAIL;
    }

    int last_err = 0;
    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) { last_err = errno; continue; }

        int restore_blocking = 0;

        if (want_nonblock) {
            if (set_nonblock(fd, 1) < 0) { last_err = errno; close(fd); fd=-1; continue; }
        } else {
            if (timeout_ms >= 0) { if (set_nonblock(fd, 1) < 0) { last_err=errno; close(fd); fd=-1; continue; } restore_blocking = 1; }
        }

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            /* connected */
        } else if (errno == EINPROGRESS) {
            if (timeout_ms >= 0) {
                int pr = poll_wait(fd, POLLOUT, timeout_ms);
                if (pr == 0) { close(fd); fd=-1; freeaddrinfo(res); return SB_EX_TIMEOUT; }
                if (pr < 0) { last_err = errno; close(fd); fd=-1; continue; }
                int soerr = 0; socklen_t sl = sizeof(soerr);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0) { last_err=errno; close(fd); fd=-1; continue; }
                if (soerr != 0) { last_err = soerr; close(fd); fd=-1; continue; }
            } else {
                if (!want_nonblock) {
                    int pr = poll_wait(fd, POLLOUT, -1);
                    if (pr < 0) { last_err = errno; close(fd); fd=-1; continue; }
                    int soerr = 0; socklen_t sl = sizeof(soerr);
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0) { last_err=errno; close(fd); fd=-1; continue; }
                    if (soerr != 0) { last_err = soerr; close(fd); fd=-1; continue; }
                } /* else: return connecting fd */
            }
        } else {
            last_err = errno; close(fd); fd=-1; continue;
        }

        if (restore_blocking) (void)set_nonblock(fd, 0);
        if (set_sh_var_int(varfd, fd) != 0) { close(fd); fd=-1; freeaddrinfo(res); return SB_EX_FAIL; }
        freeaddrinfo(res);
        return SB_EX_OK;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        builtin_error("connect: %s", strerror(last_err ? last_err : ECONNREFUSED));
        return SB_EX_FAIL;
    }
    return SB_EX_FAIL;
}

/* SEND */
static int cmd_send(int argc, char **argv) {
    int i = 0;
    int use_b64 = 0;

    while (i < argc && arg_is_flag(argv[i])) {
        if (strcmp(argv[i], "-b64") == 0) { use_b64 = 1; i++; continue; }
        break;
    }
    if (argc - i < 2) { print_usage(); return SB_EX_USAGE; }
    int fd;
    if (parse_int(argv[i], &fd) != 0 || fd < 0) { print_usage(); return SB_EX_USAGE; }
    i++;
    if (i < argc && arg_is_ddash(argv[i])) i++;

    if (argc - i < 1) { print_usage(); return SB_EX_USAGE; }

    ssize_t wr = -1;
    if (use_b64) {
        size_t blen = 0;
        unsigned char *buf = b64_decode_concat((const char *const *)&argv[i], argc - i, &blen);
        if (!buf) { builtin_error("send: base64 decode failed"); return SB_EX_FAIL; }
        wr = write_full_poll(fd, buf, blen);
        free(buf);
    } else {
        size_t tot = 0;
        for (int j=i; j<argc; j++) tot += strlen(argv[j]) + (j+1<argc ? 1U : 0U);
        char *buf = (char*)malloc(tot ? tot : 1U);
        if (!buf) { builtin_error("send: out of memory"); return SB_EX_FAIL; }
        char *p = buf;
        for (int j=i; j<argc; j++) {
            size_t l = strlen(argv[j]);
            memcpy(p, argv[j], l); p += l;
            if (j+1<argc) *p++ = ' ';
        }
        wr = write_full_poll(fd, buf, tot);
        free(buf);
    }
    if (wr < 0) { builtin_error("send: %s", strerror(errno)); return SB_EX_FAIL; }
    return SB_EX_OK;
}

/* --- recv helpers --- */
static int read_line(int fd, char **out, size_t max, int timeout_ms) {
    size_t cap = (max && max<4096U) ? max : 4096U;
    char *buf = (char*)malloc(cap ? cap : 1U);
    if (!buf) { builtin_error("recv: OOM"); return SB_EX_FAIL; }
    size_t len = 0;

    for (;;) {
        if (max && len >= max) break;

        int pr = poll_wait(fd, POLLIN, timeout_ms);
        if (pr == 0 && len == 0) { free(buf); return SB_EX_TIMEOUT; }
        if (pr == 0 && len > 0) { break; }
        if (pr < 0) { free(buf); builtin_error("recv: poll: %s", strerror(errno)); return SB_EX_FAIL; }

        char tmp[1024];
        ssize_t r = read(fd, tmp, sizeof(tmp));
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && ERRNO_AGAIN(errno)) continue;
        if (r < 0) { free(buf); builtin_error("recv: %s", strerror(errno)); return SB_EX_FAIL; }
        if (r == 0) break;

        for (ssize_t k=0; k<r; k++) {
            if (max && len >= max) break;
            if (len+1U > cap) { size_t ncap = cap*2U; if (max && ncap>max) ncap=max; char *nb = realloc(buf, ncap? ncap:1U); if (!nb){free(buf);builtin_error("recv: OOM");return SB_EX_FAIL;} buf=nb; cap=ncap; }
            buf[len++] = tmp[k];
            if (tmp[k] == '\n') { *out = buf; return (int)len; }
        }
    }
    *out = buf;
    return (int)len;
}

static int read_bytes(int fd, char **out, size_t need, int timeout_ms) {
    size_t cap = need ? need : 4096U;
    char *buf = (char*)malloc(cap ? cap : 1U);
    if (!buf) { builtin_error("recv: OOM"); return SB_EX_FAIL; }
    size_t len = 0;

    for (;;) {
        if (need && len >= need) break;
        int pr = poll_wait(fd, POLLIN, timeout_ms);
        if (pr == 0 && len == 0) { free(buf); return SB_EX_TIMEOUT; }
        if (pr == 0 && len > 0) { break; }
        if (pr < 0) { free(buf); builtin_error("recv: poll: %s", strerror(errno)); return SB_EX_FAIL; }

        ssize_t r = read(fd, buf+len, (need ? (need-len) : (cap-len)));
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && ERRNO_AGAIN(errno)) continue;
        if (r < 0) { free(buf); builtin_error("recv: %s", strerror(errno)); return SB_EX_FAIL; }
        if (r == 0) break;
        len += (size_t)r;
        if (!need && len == cap) { size_t ncap = cap * 2U; char *nb = realloc(buf, ncap? ncap:1U); if (!nb){free(buf);builtin_error("recv: OOM");return SB_EX_FAIL;} buf=nb; cap=ncap; }
    }
    *out = buf;
    return (int)len;
}

static int read_all(int fd, char **out, size_t max, int timeout_ms) {
    size_t cap = 4096U;
    char *buf = (char*)malloc(cap);
    if (!buf) { builtin_error("recv: OOM"); return SB_EX_FAIL; }
    size_t len = 0;

    for (;;) {
        int pr = poll_wait(fd, POLLIN, timeout_ms);
        if (pr == 0 && len == 0) { free(buf); return SB_EX_TIMEOUT; }
        if (pr == 0 && len > 0) { break; }
        if (pr < 0) { free(buf); builtin_error("recv: poll: %s", strerror(errno)); return SB_EX_FAIL; }

        ssize_t r = read(fd, buf+len, (cap-len));
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && ERRNO_AGAIN(errno)) continue;
        if (r < 0) { free(buf); builtin_error("recv: %s", strerror(errno)); return SB_EX_FAIL; }
        if (r == 0) break;
        len += (size_t)r;
        if (max && len >= max) { break; }
        if (len == cap) { size_t ncap = cap * 2U; if (max && ncap > max) ncap = max; char *nb = realloc(buf, ncap? ncap:1U); if (!nb){free(buf);builtin_error("recv: OOM");return SB_EX_FAIL;} buf=nb; cap=ncap; }
    }
    *out = buf;
    return (int)len;
}

/* RECV */
static int cmd_recv(int argc, char **argv) {
    int i = 0;
    int timeout_ms = -1;
    size_t max = 0;
    enum { M_LINE, M_BYTES, M_ALL } mode = M_LINE;

    while (i < argc && arg_is_flag(argv[i])) {
        if (strcmp(argv[i], "-T") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            unsigned u; if (parse_uint(argv[i+1], &u)) { print_usage(); return SB_EX_USAGE; }
            timeout_ms = (int)u; i+=2; continue;
        }
        if (strcmp(argv[i], "-max") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            unsigned u; if (parse_uint(argv[i+1], &u)) { print_usage(); return SB_EX_USAGE; }
            max = (size_t)u; i+=2; continue;
        }
        if (strcmp(argv[i], "-mode") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            if (strcmp(argv[i+1], "line")==0) mode = M_LINE;
            else if (strcmp(argv[i+1], "bytes")==0) mode = M_BYTES;
            else if (strcmp(argv[i+1], "all")==0) mode = M_ALL;
            else { print_usage(); return SB_EX_USAGE; }
            i+=2; continue;
        }
        break;
    }
    if (argc - i != 2) { print_usage(); return SB_EX_USAGE; }

    int fd; if (parse_int(argv[i], &fd) || fd < 0) { print_usage(); return SB_EX_USAGE; }
    const char *var = argv[i+1];

    char *buf = NULL;
    int rlen = 0;

    if (mode == M_LINE) {
        rlen = read_line(fd, &buf, max, timeout_ms);
    } else if (mode == M_BYTES) {
        size_t need = max ? max : 4096U;
        rlen = read_bytes(fd, &buf, need, timeout_ms);
    } else {
        rlen = read_all(fd, &buf, max, timeout_ms);
    }

    if (rlen == SB_EX_TIMEOUT) {
        /* timeout with no data */
        if (set_sh_var(var, "") != 0) { free(buf); return SB_EX_FAIL; }
        free(buf);
        return SB_EX_TIMEOUT;
    }
    if (rlen == SB_EX_FAIL) {
        /* helper already freed buf (or never allocated) */
        return SB_EX_FAIL;
    }
    if (rlen < 0 || buf == NULL) {
        /* defensive: shouldn't happen with current helpers */
        if (buf) free(buf);
        builtin_error("recv: internal error");
        return SB_EX_FAIL;
    }

    /* Bash vars can't hold NUL: store up to first NUL */
    size_t slen = 0;
    while ((size_t)slen < (size_t)rlen && buf[slen] != '\0') slen++;
    char *tmp = (char*)malloc(slen+1U);
    if (!tmp) { free(buf); builtin_error("recv: OOM"); return SB_EX_FAIL; }
    memcpy(tmp, buf, slen); tmp[slen] = '\0';
    int sv = set_sh_var(var, tmp);
    free(tmp);
    free(buf);
    if (sv != 0) return SB_EX_FAIL;
    return SB_EX_OK;
}

/* CLOSE */
static int cmd_close(int argc, char **argv) {
    if (argc != 1) { print_usage(); return SB_EX_USAGE; }
    int fd; if (parse_int(argv[0], &fd) || fd < 0) { print_usage(); return SB_EX_USAGE; }
    if (close(fd) == 0) return SB_EX_OK;
    if (errno == EBADF) return SB_EX_FAIL; /* 1 if it wasn't open */
    builtin_error("close: %s", strerror(errno));
    return SB_EX_FAIL;
}

/* LISTEN */
static int cmd_listen(int argc, char **argv) {
    int i=0;
    int backlog = 128;
    const char *addr = NULL;
    unsigned short port = 0;

    while (i < argc && arg_is_flag(argv[i])) {
        if (strcmp(argv[i], "-b") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            unsigned u; if (parse_uint(argv[i+1], &u)) { print_usage(); return SB_EX_USAGE; }
            backlog = (int)u; i+=2; continue;
        }
        if (strcmp(argv[i], "-a") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            addr = argv[i+1]; i+=2; continue;
        }
        if (strcmp(argv[i], "-p") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            unsigned short p; if (parse_uint16(argv[i+1], &p)) { print_usage(); return SB_EX_USAGE; }
            port = p; i+=2; continue;
        }
        break;
    }
    if (argc - i != 1) { print_usage(); return SB_EX_USAGE; }
    const char *varfd = argv[i];
    if (port == 0) { print_usage(); return SB_EX_USAGE; }

    char portstr[16]; snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);

    struct addrinfo hints, *res=NULL, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int gai = getaddrinfo(addr ? addr : "::", portstr, &hints, &res);
    if (gai != 0) {
        gai = getaddrinfo(addr ? addr : "0.0.0.0", portstr, &hints, &res);
        if (gai != 0) {
            builtin_error("listen: getaddrinfo(%s,%s): %s", addr?addr:"*", portstr, gai_strerror(gai));
            return SB_EX_FAIL;
        }
    }

    int fd = -1;
    int last_err = 0;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) { last_err = errno; continue; }
        int on = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        if (rp->ai_family == AF_INET6) {
            int v6only = 0; (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
        }

        if (bind(fd, rp->ai_addr, rp->ai_addrlen) < 0) { last_err = errno; close(fd); fd=-1; continue; }
        if (listen(fd, backlog) < 0) { last_err = errno; close(fd); fd=-1; continue; }

        if (set_sh_var_int(varfd, fd) != 0) { close(fd); fd=-1; freeaddrinfo(res); return SB_EX_FAIL; }
        freeaddrinfo(res);
        return SB_EX_OK;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        builtin_error("listen: %s", strerror(last_err ? last_err : EADDRINUSE));
        return SB_EX_FAIL;
    }
    return SB_EX_FAIL;
}

/* ACCEPT */
static int cmd_accept(int argc, char **argv) {
    int i=0;
    int timeout_ms = -1;

    while (i < argc && arg_is_flag(argv[i])) {
        if (strcmp(argv[i], "-T") == 0) {
            if (i+1 >= argc) { print_usage(); return SB_EX_USAGE; }
            unsigned u; if (parse_uint(argv[i+1], &u)) { print_usage(); return SB_EX_USAGE; }
            timeout_ms = (int)u; i+=2; continue;
        }
        break;
    }
    if (argc - i < 2 || argc - i > 3) { print_usage(); return SB_EX_USAGE; }
    int lfd; if (parse_int(argv[i], &lfd) || lfd < 0) { print_usage(); return SB_EX_USAGE; }
    const char *varfd = argv[i+1];
    const char *varpeer = (argc - i == 3) ? argv[i+2] : NULL;

    if (timeout_ms >= 0) {
        int pr = poll_wait(lfd, POLLIN, timeout_ms);
        if (pr == 0) return SB_EX_TIMEOUT;
        if (pr < 0) { builtin_error("accept: poll: %s", strerror(errno)); return SB_EX_FAIL; }
    }

    struct sockaddr_storage ss; socklen_t sl = sizeof(ss);
    int cfd = accept(lfd, (struct sockaddr*)&ss, &sl);
    if (cfd < 0) { builtin_error("accept: %s", strerror(errno)); return SB_EX_FAIL; }

    if (set_sh_var_int(varfd, cfd) != 0) { close(cfd); return SB_EX_FAIL; }

    if (varpeer) {
        char h[NI_MAXHOST], s[NI_MAXSERV];
        if (getnameinfo((struct sockaddr*)&ss, sl, h, sizeof(h), s, sizeof(s),
                        NI_NUMERICHOST|NI_NUMERICSERV) == 0) {
            char hp[NI_MAXHOST+NI_MAXSERV+2];
            snprintf(hp, sizeof(hp), "%s:%s", h, s);
            (void)set_sh_var(varpeer, hp);
        } else {
            (void)set_sh_var(varpeer, "");
        }
    }
    return SB_EX_OK;
}

/* DISPATCH (define before socket_builtin) */
static int socket_dispatch(int argc, char **argv) {
    if (argc < 1) { print_usage(); return SB_EX_USAGE; }
    const char *sub = argv[0];
    argv++; argc--;

    if      (strcmp(sub, "connect")==0) return cmd_connect(argc, argv);
    else if (strcmp(sub, "send")==0)    return cmd_send(argc, argv);
    else if (strcmp(sub, "recv")==0)    return cmd_recv(argc, argv);
    else if (strcmp(sub, "close")==0)   return cmd_close(argc, argv);
    else if (strcmp(sub, "listen")==0)  return cmd_listen(argc, argv);
    else if (strcmp(sub, "accept")==0)  return cmd_accept(argc, argv);

    print_usage();
    return SB_EX_USAGE;
}

/* Bash entrypoint */
int socket_builtin(WORD_LIST *list) {
    char **argv = NULL;
    int argc = list_to_argv(list, &argv);
    if (argc < 0) { builtin_error("OOM"); return SB_EX_FAIL; }
    int rc = socket_dispatch(argc, argv);
    free_argv(argc, argv);
    return rc;
}

/* Writable arrays for -Wwrite-strings friendliness */
static char name_socket[]   = "socket";
static char short_doc_str[] = "socket (connect|send|recv|close|listen|accept) ...";
static char doc0[]          = "Minimal TCP socket API for Bash.";
static char doc1[]          = "See: socket connect|send|recv|close|listen|accept";
static char *socket_doc[]   = { doc0, doc1, NULL };

struct builtin socket_struct = {
    .name      = name_socket,
    .function  = socket_builtin,
    .flags     = BUILTIN_ENABLED,
    .long_doc  = socket_doc,
    .short_doc = short_doc_str,
    .handle    = 0
};
