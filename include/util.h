// MIT License
#ifndef SOCKET_BUILTIN_UTIL_H
#define SOCKET_BUILTIN_UTIL_H

#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

/* Normalize "would block" errno comparison for -Wlogical-op cleanliness */
#ifndef ERRNO_AGAIN
# if defined(EAGAIN) && defined(EWOULDBLOCK) && (EAGAIN == EWOULDBLOCK)
#  define ERRNO_AGAIN(e) ((e) == EAGAIN)
# else
#  define ERRNO_AGAIN(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
# endif
#endif

// Toggle O_NONBLOCK on fd; returns 0 or -1 with errno set.
static inline int set_nonblock(int fd, int on) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    if (on) fl |= O_NONBLOCK; else fl &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, fl);
}

static inline int poll_wait(int fd, short events, int timeout_ms) {
    struct pollfd p = { .fd = fd, .events = events, .revents = 0 };
    for (;;) {
        int r = poll(&p, 1, timeout_ms);
        if (r < 0 && errno == EINTR) continue;
        return r; // 0 timeout, >0 ready, <0 error
    }
}

static inline ssize_t write_full_poll(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t left = len;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w > 0) { p += (size_t)w; left -= (size_t)w; continue; }
        if (w < 0 && (errno == EINTR)) continue;
        if (w < 0 && ERRNO_AGAIN(errno)) {
            int pr = poll_wait(fd, POLLOUT, -1);
            if (pr <= 0) return -1;
            continue;
        }
        return -1;
    }
    return (ssize_t)len;
}

// minimal base64 decoder (unchanged)
static inline unsigned char *b64_decode_concat(const char *const *args, int argc, size_t *outlen) {
    static const unsigned char T[256] = {
        ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,['I']=8,['J']=9,
        ['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,['Q']=16,['R']=17,['S']=18,['T']=19,
        ['U']=20,['V']=21,['W']=22,['X']=23,['Y']=24,['Z']=25,
        ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,['i']=34,['j']=35,
        ['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,
        ['u']=46,['v']=47,['w']=48,['x']=49,['y']=50,['z']=51,
        ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,
        ['+']=62, ['/']=63
    };
    size_t tot = 0;
    for (int i=0;i<argc;i++) tot += strlen(args[i]);
    char *cat = (char*)malloc(tot+1);
    if (!cat) return NULL;
    char *cp = cat;
    for (int i=0;i<argc;i++) { size_t l=strlen(args[i]); memcpy(cp,args[i],l); cp+=l; }
    *cp = '\0';

    size_t n = strlen(cat);
    size_t pad = 0;
    if (n >= 1 && cat[n-1] == '=') pad++;
    if (n >= 2 && cat[n-2] == '=') pad++;
    size_t outcap = (n/4)*3 - pad;
    unsigned char *out = (unsigned char*)malloc(outcap ? outcap : 1);
    if (!out) { free(cat); return NULL; }

    size_t oi = 0;
    for (size_t i=0; i<n; ) {
        unsigned char c0 = cat[i++] ; if (c0=='=') break;
        unsigned char c1 = cat[i++] ; if (c1=='=') break;
        unsigned char c2 = (i<n)?cat[i++]: '=';
        unsigned char c3 = (i<n)?cat[i++]: '=';
        if (!T[c0] && c0!='A') { free(out); free(cat); return NULL; }
        if (!T[c1] && c1!='A') { free(out); free(cat); return NULL; }
        unsigned v0 = T[c0], v1 = T[c1];
        unsigned v2 = (c2=='=')?0:T[c2];
        unsigned v3 = (c3=='=')?0:T[c3];
        unsigned triple = (v0<<18) | (v1<<12) | (v2<<6) | v3;
        if (oi < outcap) out[oi++] = (triple >> 16) & 0xFF;
        if (c2!='=' && oi < outcap) out[oi++] = (triple >> 8) & 0xFF;
        if (c3!='=' && oi < outcap) out[oi++] = triple & 0xFF;
        if (c3=='=' || c2=='=') break;
    }
    *outlen = oi;
    free(cat);
    return out;
}

#endif
