// MIT License
#ifndef SOCKET_BUILTIN_ARGPARSE_H
#define SOCKET_BUILTIN_ARGPARSE_H

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>

static inline int arg_is_flag(const char *s) {
    return s && s[0] == '-' && s[1] != '\0';
}
static inline int arg_is_ddash(const char *s) {
    return s && strcmp(s, "--") == 0;
}

static inline int parse_uint(const char *s, unsigned *out) {
    if (!s || !*s) return EINVAL;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || !end || *end) return EINVAL;
    if (v > UINT_MAX) return ERANGE;
    *out = (unsigned)v;
    return 0;
}

static inline int parse_uint16(const char *s, unsigned short *out) {
    unsigned u; int e = parse_uint(s, &u);
    if (e) return e;
    if (u > 65535u) return ERANGE;
    *out = (unsigned short)u;
    return 0;
}

static inline int parse_int(const char *s, int *out) {
    if (!s || !*s) return EINVAL;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno || !end || *end) return EINVAL;
    if (v > INT_MAX || v < INT_MIN) return ERANGE;
    *out = (int)v;
    return 0;
}

#endif
