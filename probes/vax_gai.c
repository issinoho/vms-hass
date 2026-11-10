/* vax_gai.c - which getaddrinfo() hints TCP/IP Services V5.1 for VAX accepts.
   vax.sh: cc/prefix=all vax_gai, link, run vax_gai <host> <port> */
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <socket.h>
#include <in.h>

int main(int argc, char **argv)
{
    static const int fam[] = { AF_UNSPEC, AF_INET }, proto[] = { 0, IPPROTO_TCP };
    int i, j, k;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 2; k++) {
                struct addrinfo h, *r = 0, *a;
                int rc, n = 0;
                memset(&h, 0, sizeof h);
                h.ai_family = fam[i];
                h.ai_socktype = k ? SOCK_STREAM : 0;
                h.ai_protocol = proto[j];
                rc = getaddrinfo(argv[1], argv[2], &h, &r);
                for (a = r; a; a = a->ai_next)
                    n++;
                printf("family %s protocol %d socktype %d: rc %d (%s), %d results",
                       i ? "INET" : "UNSPEC", proto[j], h.ai_socktype, rc,
                       gai_strerror(rc) ? gai_strerror(rc) : "NULL", n);
                if (r)
                    printf(", first family %d socktype %d protocol %d len %d",
                           r->ai_family, r->ai_socktype, r->ai_protocol, (int)r->ai_addrlen);
                printf("\n");
                if (r)
                    freeaddrinfo(r);
            }
    {
        struct hostent *he = gethostbyname(argv[1]);
        printf("gethostbyname: %s\n", he ? "ok" : "failed");
    }
    return 1;
}
