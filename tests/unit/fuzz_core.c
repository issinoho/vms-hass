/* fuzz_core.c - random and mutated broker input to the decoder and the session.
 *
 *   fuzz_core <iterations>
 *
 * Built with the address and undefined-behaviour sanitizers (make fuzz): any
 * memory error or overflow aborts the run.  Inputs mix valid packets the
 * broker sends with random bytes, cut at random points.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha_agent.h"
#include "ha_mqtt.h"
#include "ha_sess.h"

static unsigned long rng = 88172645463325252UL;

static unsigned rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng >> 11);
}

static const unsigned char seeds[][32] = {
    { 0x20, 2, 0, 0 },                                       /* CONNACK */
    { 0x40, 2, 0, 1 },                                       /* PUBACK */
    { 0x90, 3, 0, 2, 0 },                                    /* SUBACK */
    { 0xD0, 0 },                                             /* PINGRESP */
    { 0x30, 28, 0, 20, 'h', 'o', 'm', 'e', 'a', 's', 's', 'i', 's', 't', 'a', 'n', 't',
      '/', 's', 't', 'a', 't', 'u', 's', 'o', 'n', 'l', 'i', 'n', 'e' },
    { 0x32, 9, 0, 1, 'x', 0, 7, 'a', 'b', 'c', 'd' },        /* PUBLISH QoS 1 */
};
static const size_t seed_len[] = { 4, 4, 5, 2, 30, 11 };

int main(int argc, char **argv)
{
    static ha_agent a;
    static struct ha_agent_cfg cfg;
    static struct ha_identity id;
    static struct ha_sample smp;
    unsigned char buf[512];
    long iters = argc > 1 ? atol(argv[1]) : 100000, it;
    long ups = 0;

    memset(&cfg, 0, sizeof cfg);
    cfg.model.discovery_prefix = "homeassistant";
    cfg.model.topic_prefix = "openvms";
    cfg.model.node_id = "f_1";
    cfg.model.device_name = "F";
    cfg.model.agent_version = "fuzz";
    cfg.sess.client_id = "fuzz";
    cfg.sess.keepalive_s = 30;
    cfg.sess.status_topic = "homeassistant/status";
    cfg.interval_s = 30;
    strcpy(smp.disk[0].name, "DKA0:");
    smp.ndisk = 1;
    strcpy(smp.nic[0].name, "EWA0");
    smp.nnic = 1;
    ha_agent_init(&a, &cfg, &id, NULL, NULL, 0);
    ha_agent_sample(&a, &smp, 0);

    for (it = 0; it < iters; it++) {
        size_t n = 0, k;
        uint64_t now = (uint64_t)it * 100;
        int pieces = 1 + (int)(rnd() % 6), p;
        if (!ha_agent_up(&a))
            ha_agent_begin(&a, now);
        /* a stream: valid packets, some mutated, and random bytes */
        for (p = 0; p < pieces && n < sizeof buf - 40; p++) {
            unsigned r = rnd() % 10;
            if (r < 6) {
                k = rnd() % (sizeof seeds / sizeof seeds[0]);
                memcpy(buf + n, seeds[k], seed_len[k]);
                if (rnd() % 4 == 0)
                    buf[n + rnd() % seed_len[k]] ^= (unsigned char)(1u << (rnd() % 8));
                n += seed_len[k];
            } else {
                size_t m = 1 + rnd() % 32;
                for (k = 0; k < m; k++)
                    buf[n++] = (unsigned char)rnd();
            }
        }
        /* delivered in random cuts */
        for (k = 0; k < n;) {
            size_t cut = 1 + rnd() % (n - k);
            if (!ha_agent_input(&a, buf + k, cut, now)) {
                ha_agent_closed(&a, now);
                break;
            }
            k += cut;
        }
        if (ha_agent_up(&a))
            ups++;
        if (!ha_agent_tick(&a, now + rnd() % 70000))
            ha_agent_closed(&a, now);
        if (rnd() % 8 == 0) {
            smp.cpu.idle += rnd() % 1000;
            smp.ndisk = (int)(rnd() % 2);
            ha_agent_sample(&a, &smp, now);
        }
        ha_buf_clear(ha_agent_out(&a));
    }
    ha_agent_free(&a);
    printf("%ld iterations, up after %ld of them, no sanitizer findings\n", iters, ups);
    return 0;
}
