/* mqtt_probe.c - Phase 0 probe: one hand-built MQTT 3.1.1 session that makes a test
 * device appear in Home Assistant through MQTT device discovery, or removes it.
 *
 *   mqtt_probe <host> <port> <user> <password-file> publish|delete [tag]
 *
 * publish: CONNECT (with a retained "offline" will), then QoS 1 PUBLISHes of the
 *          retained device-discovery config, a retained "online" and one state
 *          document, each acknowledged by PUBACK, then DISCONNECT.
 * delete:  empty retained messages on the config and availability topics, which
 *          make HA remove the device and clear the broker's retained copies.
 *
 * Packet layouts: MQTT 3.1.1 (OASIS, 2014) sections 2.2 (fixed header and remaining
 * length), 3.1 (CONNECT), 3.2 (CONNACK), 3.3 (PUBLISH), 3.4 (PUBACK), 3.14 (DISCONNECT).
 * Discovery: https://www.home-assistant.io/integrations/mqtt/ "Device discovery payload".
 * Builds on Linux and with VSI C; the VMS build reads the figures with $GETSYI.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#ifdef __VMS
#include <descrip.h>
#include <starlet.h>
#include <syidef.h>
#include <efndef.h>
#endif

static unsigned char pkt[4096];
static int sock = -1;

static void die(const char *what)
{
    fprintf(stderr, "PROBE-FAIL %s\n", what);
    if (sock >= 0)
        close(sock);
    exit(EXIT_FAILURE);
}

static size_t put_u16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)v;
    return 2;
}

static size_t put_str(unsigned char *p, const char *s)      /* 1.5.3: length + UTF-8 */
{
    size_t n = strlen(s);
    put_u16(p, (unsigned)n);
    memcpy(p + 2, s, n);
    return n + 2;
}

/* fixed header + remaining length (2.2.3) + body, sent in one go */
static void send_packet(unsigned char type, const unsigned char *body, size_t len)
{
    unsigned char hdr[5];
    size_t h = 0, n = len;
    hdr[h++] = type;
    do {
        unsigned char d = (unsigned char)(n % 128);
        n /= 128;
        hdr[h++] = (unsigned char)(d | (n ? 0x80 : 0));
    } while (n);
    if (send(sock, (const char *)hdr, (int)h, 0) != (int)h ||
        (len && send(sock, (const char *)body, (int)len, 0) != (int)len))
        die("send");
}

static void recv_all(unsigned char *p, size_t n)
{
    while (n) {
        int r = recv(sock, (char *)p, (int)n, 0);
        if (r <= 0)
            die("recv (the broker closed the connection)");
        p += r;
        n -= (size_t)r;
    }
}

/* read one packet; returns its type byte, body in pkt */
static unsigned char recv_packet(size_t *len)
{
    unsigned char b, type;
    size_t n = 0, mul = 1;
    recv_all(&type, 1);
    do {
        recv_all(&b, 1);
        n += (b & 127) * mul;
        mul *= 128;
    } while (b & 0x80);
    if (n > sizeof pkt)
        die("packet too large");
    recv_all(pkt, n);
    *len = n;
    return type;
}

static unsigned next_id = 1;

static void publish(const char *topic, const char *payload, int retain)
{
    unsigned char body[4096];
    size_t n = 0, len, plen = strlen(payload);
    unsigned id = next_id++;
    if (strlen(topic) + plen + 4 > sizeof body)
        die("payload too large");
    n += put_str(body + n, topic);
    n += put_u16(body + n, id);                 /* QoS 1 has a packet identifier */
    memcpy(body + n, payload, plen);
    n += plen;
    send_packet((unsigned char)(0x30 | 0x02 | (retain ? 1 : 0)), body, n);   /* 3.3.1 */
    if (recv_packet(&len) != 0x40 || len != 2 || ((pkt[0] << 8) | pkt[1]) != (int)id)
        die("no matching PUBACK");
    printf("PUBACK %u %s (%lu bytes%s)\n", id, topic, (unsigned long)plen,
           retain ? ", retained" : "");
}

/* the figures: node name, VMS version, uptime in seconds, active CPUs */
static void figures(char *node, size_t ncap, char *version, size_t vcap,
                    long *uptime, int *cpus)
{
#ifdef __VMS
    struct { unsigned short len, code; void *buf; unsigned short *rlen; } il[5];
    struct { unsigned short st, x; unsigned int y; } iosb;
    char nb[16], vb[9];
    unsigned short nl = 0, vl = 0;
    unsigned int avail = 0;
    unsigned __int64 boot = 0, now = 0;
    il[0].len = sizeof nb - 1; il[0].code = SYI$_NODENAME;      il[0].buf = nb;     il[0].rlen = &nl;
    il[1].len = 8;             il[1].code = SYI$_VERSION;       il[1].buf = vb;     il[1].rlen = &vl;
    il[2].len = 4;             il[2].code = SYI$_AVAILCPU_CNT;  il[2].buf = &avail; il[2].rlen = 0;
    il[3].len = 8;             il[3].code = SYI$_BOOTTIME;      il[3].buf = &boot;  il[3].rlen = 0;
    memset(&il[4], 0, sizeof il[4]);
    if (!(sys$getsyiw(EFN$C_ENF, 0, 0, il, (void *)&iosb, 0, 0) & 1) || !(iosb.st & 1))
        die("$GETSYI");
    sys$gettim((void *)&now);
    nb[nl] = 0;
    vb[vl] = 0;
    while (vl && vb[vl - 1] == ' ')
        vb[--vl] = 0;
    snprintf(node, ncap, "%s", nb);
    snprintf(version, vcap, "OpenVMS %s", vb);
    *uptime = (long)((now - boot) / 10000000);  /* 100 ns units */
    *cpus = (int)avail;
#else
    gethostname(node, ncap);
    node[ncap - 1] = 0;
    snprintf(version, vcap, "Linux (probe test)");
    *uptime = 12345;
    *cpus = 4;
#endif
}

int main(int argc, char **argv)
{
    char pw[128], node[64], id[96], version[64], topic[160], doc[3072], avail_t[160];
    struct sockaddr_in sa;
    unsigned char body[512];
    size_t n = 0, len;
    long uptime;
    int cpus, del;
    FILE *f;
    char *p;

    if (argc < 6 || (strcmp(argv[5], "publish") && strcmp(argv[5], "delete")))
        die("usage: mqtt_probe host port user password-file publish|delete [tag]");
    del = !strcmp(argv[5], "delete");

    if (!(f = fopen(argv[4], "r")) || !fgets(pw, sizeof pw, f))
        die("cannot read the password file");
    fclose(f);
    pw[strcspn(pw, "\r\n")] = 0;

    figures(node, sizeof node, version, sizeof version, &uptime, &cpus);
    if (argc > 6)
        snprintf(node, sizeof node, "%s", argv[6]);
    snprintf(id, sizeof id, "vmshass_probe_%s", node);
    for (p = id; *p; p++)
        *p = isalnum((unsigned char)*p) ? (char)tolower((unsigned char)*p) : '_';
    snprintf(avail_t, sizeof avail_t, "openvms/%s/availability", id);

    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)atoi(argv[2]));
    if ((sa.sin_addr.s_addr = inet_addr(argv[1])) == INADDR_NONE) {
        struct hostent *h = gethostbyname(argv[1]);
        if (!h)
            die("cannot resolve the broker");
        memcpy(&sa.sin_addr, h->h_addr_list[0], 4);
    }
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        die("socket");
    if (connect(sock, (struct sockaddr *)&sa, sizeof sa) < 0)
        die("TCP connect to the broker");
    printf("TCP connected to the broker\n");

    /* CONNECT (3.1): clean session, will QoS 1 retained, user name and password */
    n += put_str(body + n, "MQTT");
    body[n++] = 4;                              /* protocol level 4 = 3.1.1 */
    body[n++] = 0xC0 | 0x20 | 0x08 | 0x04 | 0x02;   /* user, pw, will retain, will QoS 1, will, clean */
    n += put_u16(body + n, 60);                 /* keepalive seconds */
    n += put_str(body + n, id);                 /* client identifier */
    n += put_str(body + n, avail_t);            /* will topic */
    n += put_str(body + n, "offline");          /* will message */
    n += put_str(body + n, argv[3]);
    n += put_str(body + n, pw);
    memset(pw, 0, sizeof pw);
    send_packet(0x10, body, n);
    if (recv_packet(&len) != 0x20 || len != 2)
        die("no CONNACK");
    printf("CONNACK return code %d\n", pkt[1]);
    if (pkt[1] != 0)
        die("the broker refused the connection");

    snprintf(topic, sizeof topic, "homeassistant/device/%s/config", id);
    if (del) {
        publish(topic, "", 1);
        publish(avail_t, "", 1);
    } else {
        snprintf(doc, sizeof doc,
            "{\"dev\":{\"ids\":[\"%s\"],\"name\":\"VMS probe %s\",\"mf\":\"VMS Software\","
            "\"mdl\":\"vms-hass Phase 0 probe\",\"sw\":\"%s\"},"
            "\"o\":{\"name\":\"vms-hass\",\"sw\":\"0.0.0-probe\","
            "\"url\":\"https://github.com/issinoho/vms-hass\"},"
            "\"cmps\":{"
            "\"uptime\":{\"p\":\"sensor\",\"unique_id\":\"%s_uptime\",\"name\":\"Uptime\","
            "\"device_class\":\"duration\",\"unit_of_measurement\":\"s\","
            "\"state_class\":\"measurement\",\"value_template\":\"{{ value_json.uptime }}\"},"
            "\"cpus\":{\"p\":\"sensor\",\"unique_id\":\"%s_cpus\",\"name\":\"Active CPUs\","
            "\"state_class\":\"measurement\",\"icon\":\"mdi:cpu-64-bit\","
            "\"value_template\":\"{{ value_json.cpus }}\"}},"
            "\"state_topic\":\"openvms/%s/state\",\"availability_topic\":\"%s\",\"qos\":1}",
            id, node, version, id, id, id, avail_t);
        publish(topic, doc, 1);
        sleep(2);                               /* let HA create the entities first */
        publish(avail_t, "online", 1);
        snprintf(topic, sizeof topic, "openvms/%s/state", id);
        snprintf(doc, sizeof doc, "{\"uptime\":%ld,\"cpus\":%d}", uptime, cpus);
        publish(topic, doc, 0);
        printf("node %s, %s, uptime %ld s, %d CPUs\n", node, version, uptime, cpus);
    }
    send_packet(0xE0, 0, 0);                    /* DISCONNECT: the will is discarded */
    close(sock);
    printf("PROBE-OK %s\n", del ? "deleted" : "published");
    return EXIT_SUCCESS;
}
