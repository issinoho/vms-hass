/* ha_net_ossl.c - TCP and TLS client on BSD sockets and OpenSSL.
 *
 * From vms-beszel 47dbcf8 bz_net_ossl.c (D4), renamed; the NOTES.md references
 * in the comments are to vms-beszel's docs/NOTES.md.  The same code runs on Linux
 * and on OpenVMS (TCP/IP Services sockets, SSL3).
 *
 * The socket is non-blocking for its whole life, and every operation has a
 * deadline: a peer that stops mid-record, or trickles a handshake, can't hang
 * the agent, whose own timers, stop file and signals keep running.
 *
 * TLS always verifies the broker's certificate and host name, against the
 * platform's CA bundle (or the system store) plus MQTT_CA_FILE.
 *
 * With HA_NO_TLS (OpenVMS VAX, which has no OpenSSL: NOTES.md 15, D11) it is
 * plain TCP only, and asking for TLS is an error.
 */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "ha_platform.h"
#include "ha_text.h"

#ifndef HA_NO_TLS
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

/* TCP/IP Services for VAX V5.1 has no socklen_t (NOTES.md 16) */
#ifdef __VAX
typedef unsigned int ha_socklen;
#else
typedef socklen_t ha_socklen;
#endif

struct ha_sock {
    int fd;
#ifndef HA_NO_TLS
    SSL *ssl;
#endif
};

#define SEND_DEADLINE_MS 30000

#ifndef HA_NO_TLS
/* One TLS context for the life of the process: loading the CA bundle took
   about 7 s of CPU on the x86 node (NOTES.md 34), and it would otherwise be
   repeated on every reconnect.  Verification still happens per connection. */
static SSL_CTX *tls_ctx;
static char tls_ctx_ca[1024];
#endif

/* wait until fd is readable (or writable) or the deadline passes: 1 ready, 0 not */
static int wait_io(int fd, int want_write, ha_u64 deadline)
{
    for (;;) {
        fd_set fs;
        struct timeval tv;
        ha_u64 now = ha_plat_now_ms();
        int rc;
        unsigned long left;
        if (now >= deadline)
            return 0;
        left = HA_TO_ULONG(deadline - now);
        FD_ZERO(&fs);
        FD_SET(fd, &fs);
        tv.tv_sec = (long)(left / 1000);
        tv.tv_usec = (long)(left % 1000) * 1000;
        rc = select(fd + 1, want_write ? NULL : &fs, want_write ? &fs : NULL, NULL, &tv);
        if (rc > 0)
            return 1;
        if (rc < 0 && errno != EINTR)
            return 0;
    }
}

#ifndef HA_NO_TLS
static void ossl_err(char *err, size_t errlen, const char *what)
{
    unsigned long e = ERR_get_error();
    char buf[200];
    if (e) {
        ERR_error_string_n(e, buf, sizeof buf);
        snprintf(err, errlen, "%s: %s", what, buf);
    } else {
        snprintf(err, errlen, "%s", what);
    }
    ERR_clear_error();
}
#endif

#ifdef __VAX
/* TCP/IP Services V5.1 for VAX: getaddrinfo() fails for every name, numeric ones
   and localhost too (11, and gai_strerror() gives NULL), while gethostbyname()
   works (probes/vax_gai.c, NOTES.md 16).  IPv4 only, as V5.1 is. */
static struct sockaddr_in vax_sin;
static struct addrinfo vax_ai;

static int vax_lookup(const char *host, const char *port, struct addrinfo **res)
{
    unsigned int a = (unsigned int)inet_addr(host);
    memset(&vax_sin, 0, sizeof vax_sin);
    vax_sin.sin_family = AF_INET;
    vax_sin.sin_port = htons((unsigned short)atoi(port));
    if (a != 0xFFFFFFFFu) {
        memcpy(&vax_sin.sin_addr, &a, 4);
    } else {
        struct hostent *he = gethostbyname(host);
        if (!he || he->h_addrtype != AF_INET || he->h_length != 4)
            return 1;
        memcpy(&vax_sin.sin_addr, he->h_addr, 4);
    }
    memset(&vax_ai, 0, sizeof vax_ai);
    vax_ai.ai_family = AF_INET;
    vax_ai.ai_socktype = SOCK_STREAM;
    vax_ai.ai_protocol = IPPROTO_TCP;
    vax_ai.ai_addr = (struct sockaddr *)&vax_sin;
    vax_ai.ai_addrlen = sizeof vax_sin;
    *res = &vax_ai;
    return 0;
}
/* netdb.h has these as macros (to the C library's names) */
#undef getaddrinfo
#undef freeaddrinfo
#undef gai_strerror
#define getaddrinfo(host, port, hints, res) vax_lookup(host, port, res)
#define freeaddrinfo(r) ((void)(r))
#define gai_strerror(rc) "no such host"
#endif

/* "IPv4 192.0.2.10" / "IPv6 2001:db8::1": errors name the address that failed
   (NOTES.md 40: a socket() error at 03:04 didn't say which one) */
static void addr_name(const struct addrinfo *ai, char *out, size_t cap)
{
    char host[64];
#ifdef __VAX
    snprintf(host, sizeof host, "%s", inet_ntoa(((const struct sockaddr_in *)ai->ai_addr)->sin_addr));
#else
    if (getnameinfo(ai->ai_addr, ai->ai_addrlen, host, sizeof host, NULL, 0, NI_NUMERICHOST) != 0)
        snprintf(host, sizeof host, "?");
#endif
    snprintf(out, cap, "%s %s", ai->ai_family == AF_INET6 ? "IPv6" : ai->ai_family == AF_INET ? "IPv4" : "address", host);
}

static int connect_timeout(const struct addrinfo *ai, unsigned timeout_ms, char *err, size_t errlen)
{
    char an[80];
    int fd, fl, rc, so = 0;
    ha_socklen sl = sizeof so;
    fd_set w;
    struct timeval tv;
    addr_name(ai, an, sizeof an);
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        snprintf(err, errlen, "socket for %s: %s", an, strerror(errno));
        return -1;
    }
    fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (rc != 0 && errno != EINPROGRESS) {
        snprintf(err, errlen, "connect to %s: %s", an, strerror(errno));
        close(fd);
        return -1;
    }
    if (rc != 0) {
        FD_ZERO(&w);
        FD_SET(fd, &w);
        tv.tv_sec = (long)(timeout_ms / 1000);
        tv.tv_usec = (long)(timeout_ms % 1000) * 1000;
        rc = select(fd + 1, NULL, &w, NULL, &tv);
        if (rc <= 0) {
            if (rc == 0)
                snprintf(err, errlen, "connect to %s: timed out", an);
            else
                snprintf(err, errlen, "select: %s", strerror(errno));
            close(fd);
            return -1;
        }
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&so, &sl);
        if (so) {
            snprintf(err, errlen, "connect to %s: %s", an, strerror(so));
            close(fd);
            return -1;
        }
    }
    return fd;                              /* still non-blocking: see the top */
}

#ifndef HA_NO_TLS
static int is_ip_literal(const char *host)
{
    unsigned char a[16];
    return inet_pton(AF_INET, host, a) == 1 || inet_pton(AF_INET6, host, a) == 1;
}

static SSL_CTX *tls_context(const char *ca_file, char *err, size_t errlen)
{
    const char *bundle = ha_plat_default_ca_file();
    SSL_CTX *ctx;
    if (!ca_file)
        ca_file = "";
    if (tls_ctx && strcmp(tls_ctx_ca, ca_file) == 0)
        return tls_ctx;
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        ossl_err(err, errlen, "TLS context");
        return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    if (bundle) {
        if (SSL_CTX_load_verify_locations(ctx, bundle, NULL) != 1) {
            ossl_err(err, errlen, "loading the CA bundle");
            SSL_CTX_free(ctx);
            return NULL;
        }
    } else if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        ossl_err(err, errlen, "loading the system CA store");
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (*ca_file && SSL_CTX_load_verify_locations(ctx, ca_file, NULL) != 1) {
        ossl_err(err, errlen, "loading MQTT_CA_FILE");
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (tls_ctx)
        SSL_CTX_free(tls_ctx);
    tls_ctx = ctx;
    ha_strlcpy(tls_ctx_ca, ca_file, sizeof tls_ctx_ca);
    return ctx;
}

static int tls_start(ha_sock *s, const char *host, const char *ca_file, unsigned timeout_ms,
                     char *err, size_t errlen)
{
    ha_u64 deadline = ha_plat_now_ms() + timeout_ms;
    int r;
    SSL_CTX *ctx = tls_context(ca_file, err, errlen);
    if (!ctx)
        return 0;
    s->ssl = SSL_new(ctx);
    if (!s->ssl) {
        ossl_err(err, errlen, "TLS session");
        return 0;
    }
    if (is_ip_literal(host)) {
        if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(s->ssl), host) != 1) {
            ossl_err(err, errlen, "TLS IP check");
            return 0;
        }
    } else {
        SSL_set_tlsext_host_name(s->ssl, host);
        if (SSL_set1_host(s->ssl, host) != 1) {
            ossl_err(err, errlen, "TLS host check");
            return 0;
        }
    }
    SSL_set_fd(s->ssl, s->fd);
    /* the whole handshake within timeout_ms, however slowly the peer sends */
    while ((r = SSL_connect(s->ssl)) != 1) {
        int e = SSL_get_error(s->ssl, r);
        if ((e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) &&
            wait_io(s->fd, e == SSL_ERROR_WANT_WRITE, deadline))
            continue;
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            snprintf(err, errlen, "TLS handshake: timed out");
            return 0;
        }
        long v = SSL_get_verify_result(s->ssl);
        if (v == X509_V_ERR_HOSTNAME_MISMATCH || v == X509_V_ERR_IP_ADDRESS_MISMATCH)
            snprintf(err, errlen, "TLS: the broker's certificate is not for %s (%s)", host,
                     X509_verify_cert_error_string(v));
        else if (v == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY ||
                 v == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT ||
                 v == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
                 v == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)
            snprintf(err, errlen, "TLS: certificate not trusted: %s (set MQTT_CA_FILE to the CA "
                     "that issued the broker's certificate)", X509_verify_cert_error_string(v));
        else if (v != X509_V_OK)
            snprintf(err, errlen, "TLS: certificate not trusted: %s", X509_verify_cert_error_string(v));
        else
            ossl_err(err, errlen, "TLS handshake");
        return 0;
    }
    return 1;
}
#endif

ha_sock *ha_net_open(const char *host, const char *port, int tls, const char *ca_file,
                     unsigned timeout_ms, char *err, size_t errlen)
{
    struct addrinfo hints, *res, *ai;
    ha_sock *s;
    int rc;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    /* VMS TCP/IP answers each address twice for SOCK_STREAM, as TCP and as SCTP,
       and socket() refuses the SCTP ones with EPROTOTYPE: that error, from the
       last entry tried, hid the real one when the broker was down (NOTES.md 45) */
    hints.ai_protocol = IPPROTO_TCP;
#ifdef HA_NO_TLS
    if (tls) {
        snprintf(err, errlen, "TLS is not available on this system: set MQTT_TLS=0");
        return NULL;
    }
#endif
    rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        snprintf(err, errlen, "lookup of %s failed: %s", host, gai_strerror(rc));
        return NULL;
    }
    s = calloc(1, sizeof *s);
    if (!s) {
        freeaddrinfo(res);
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    s->fd = -1;
    for (ai = res; ai && s->fd < 0; ai = ai->ai_next)
        s->fd = connect_timeout(ai, timeout_ms, err, errlen);
    freeaddrinfo(res);
    if (s->fd < 0) {
        free(s);
        return NULL;
    }
#ifndef HA_NO_TLS
    if (tls && !tls_start(s, host, ca_file, timeout_ms, err, errlen)) {
        ha_net_close(s);
        return NULL;
    }
#else
    (void)ca_file;
#endif
    return s;
}

int ha_net_send(ha_sock *s, const void *p, size_t n)
{
    /* all of it within SEND_DEADLINE_MS, or fail (and the caller closes) */
    const char *c = p;
    ha_u64 deadline = ha_plat_now_ms() + SEND_DEADLINE_MS;
    while (n) {
        long w;
        int want_write = 1;
#ifndef HA_NO_TLS
        if (s->ssl) {
            int r = SSL_write(s->ssl, c, n > 65536 ? 65536 : (int)n);
            if (r > 0) {
                w = r;
            } else {
                int e = SSL_get_error(s->ssl, r);
                if (e != SSL_ERROR_WANT_WRITE && e != SSL_ERROR_WANT_READ) {
                    ERR_clear_error();
                    return 0;
                }
                want_write = e == SSL_ERROR_WANT_WRITE;
                w = 0;
            }
        } else
#endif
        {
            w = (long)send(s->fd, c, n, 0);
            if (w < 0 && errno == EINTR)
                continue;
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                w = 0;
            else if (w <= 0)
                return 0;
        }
        if (w == 0) {
            if (!wait_io(s->fd, want_write, deadline))
                return 0;
            continue;
        }
        c += w;
        n -= (size_t)w;
    }
    return 1;
}

long ha_net_recv(ha_sock *s, void *p, size_t cap)
{
#ifndef HA_NO_TLS
    if (s->ssl) {
        int r = SSL_read(s->ssl, p, cap > 65536 ? 65536 : (int)cap);
        if (r > 0)
            return r;
        switch (SSL_get_error(s->ssl, r)) {
        case SSL_ERROR_ZERO_RETURN:
            return 0;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            return -2;                      /* a partial record: never blocks (M1) */
        default:
            ERR_clear_error();
            return -1;
        }
    } else
#endif
    {
        long r;
        do
            r = (long)recv(s->fd, p, cap, 0);
        while (r < 0 && errno == EINTR);
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return -2;
        return r;
    }
}

int ha_net_wait(ha_sock *s, unsigned timeout_ms)
{
    fd_set rd;
    struct timeval tv;
    int rc;
#ifndef HA_NO_TLS
    if (s->ssl && SSL_pending(s->ssl) > 0)
        return 1;
#endif
    FD_ZERO(&rd);
    FD_SET(s->fd, &rd);
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)(timeout_ms % 1000) * 1000;
    rc = select(s->fd + 1, &rd, NULL, NULL, &tv);
    if (rc < 0)
        return errno == EINTR ? 0 : -1;
    return rc > 0;
}

void ha_net_close(ha_sock *s)
{
    if (!s)
        return;
#ifndef HA_NO_TLS
    if (s->ssl) {
        SSL_shutdown(s->ssl);
        SSL_free(s->ssl);
    }
#endif
    if (s->fd >= 0)
        close(s->fd);
    free(s);
}

int ha_net_random(void *p, size_t n)
{
#ifdef HA_NO_TLS
    /* no cryptographic generator: refuse rather than give a guessable secret */
    (void)p;
    (void)n;
    return 0;
#else
    return RAND_bytes(p, (int)n) == 1;
#endif
}
