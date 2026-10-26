/* ha_main.c - the agent's program: configuration, the socket and the loop.
 *
 *   hass_agent [config-file]           run (default: the platform's config file)
 *   hass_agent --remove [config-file]  remove this node's device from HA, then exit
 *   hass_agent --print [config-file]   print the discovery and state documents
 *                                      from two samples, without connecting
 *   hass_agent --version
 *   hass_agent --random                print 32 random letters and digits (a password
 *                                      for the agent's account, which nobody types)
 *
 * Settings: docs/CONFIG.md.  All protocol logic is in src/core; this file only
 * moves bytes between the socket and ha_agent, and keeps the time.
 *
 * Running, it stops cleanly (offline published, connection closed) on SIGINT
 * or SIGTERM, or when the file "stop" appears in DATA_DIR
 * (HASS$ROOT:[DATA]STOP. on OpenVMS, made by HASS$CONTROL STOP).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ha_agent.h"
#include "ha_conf.h"
#include "ha_platform.h"
#include "ha_text.h"
#include "ha_version.h"

#define CONNECT_TIMEOUT_MS 15000
#define WAIT_SLICE_MS 1000              /* how often the stop request is checked */
#define HA_EXIT_REFUSED 44              /* SS$_ABORT: HASS$RUN.COM does not restart (D9) */

static int min_level = HA_LOG_INFO;

static void log_cb(void *ctx, int level, const char *msg)
{
    (void)ctx;
    if (level >= min_level)
        ha_plat_log(level, msg);
}

static void say(int level, const char *fmt, ...)
{
    char msg[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    log_cb(NULL, level, msg);
}

static int yes(const char *v)
{
    return v && (ha_ieq(v, "yes") || ha_ieq(v, "true") || ha_ieq(v, "1") || ha_ieq(v, "on"));
}

static int number(const char *v, long lo, long hi, long dflt, const char *name, long *out)
{
    char *end;
    long n;
    if (!v || !*v) {
        *out = dflt;
        return 1;
    }
    n = strtol(v, &end, 10);
    if (*end || n < lo || n > hi) {
        say(HA_LOG_ERROR, "%s must be a whole number from %ld to %ld", name, lo, hi);
        return 0;
    }
    *out = n;
    return 1;
}

/* the node ID: NODE_ID, or the one saved in DATA_DIR, or made now and saved
   (it must never change once published: CLAUDE.md) */
static int node_id(char *out, size_t cap, const char *set, const char *dir,
                   const struct ha_identity *id, int save)
{
    char path[1100];
    ha_buf b;
    if (set && *set) {
        if (!ha_slug(out, cap, set) || strcmp(out, set) != 0) {
            say(HA_LOG_ERROR, "NODE_ID may only contain a-z, 0-9 and single '_' between them");
            return 0;
        }
        return 1;
    }
    ha_plat_data_path(path, sizeof path, dir, "node_id");
    ha_buf_init(&b);
    if (ha_plat_read_file(path, &b)) {
        const char *s = (const char *)b.p;
        size_t n = b.len;
        ha_trim(&s, &n);
        if (n && ha_strlcpyn(out, cap, s, n)) {
            ha_buf_free(&b);
            return 1;
        }
        say(HA_LOG_ERROR, "%s is empty or too long", path);
        ha_buf_free(&b);
        return 0;
    }
    ha_buf_free(&b);
    if (ha_plat_exists(path)) {
        /* never replace a node ID we merely can't read: HA would see a new device */
        say(HA_LOG_ERROR, "cannot read the node ID in %s (check its owner and protection)", path);
        return 0;
    }
    if (!ha_node_id(out, cap, id->node, id->system_id)) {
        say(HA_LOG_ERROR, "cannot make a node ID from the node name \"%s\"", id->node);
        return 0;
    }
    if (!save)
        return 1;
    if (!ha_plat_write_file(path, out, strlen(out))) {
        say(HA_LOG_ERROR, "cannot save the node ID to %s (does DATA_DIR exist?)", path);
        return 0;
    }
    say(HA_LOG_INFO, "node ID %s saved to %s", out, path);
    return 1;
}

static void wipe(ha_buf *b)
{
    if (b->p)
        memset(b->p, 0, b->cap);
    ha_buf_free(b);
}

static int flush(ha_agent *a, ha_sock *s)
{
    ha_buf *out = ha_agent_out(a);
    if (!out->len)
        return 1;
    if (!ha_net_send(s, out->p, out->len))
        return 0;
    ha_agent_sent(a, out->len);
    return 1;
}

/* read whatever has arrived; 0 if the connection is gone */
static int pump(ha_agent *a, ha_sock *s, unsigned wait_ms)
{
    static unsigned char buf[8192];
    int w = ha_net_wait(s, wait_ms);
    if (w < 0)
        return 0;
    while (w > 0) {
        long n = ha_net_recv(s, buf, sizeof buf);
        if (n == -2)
            break;
        if (n <= 0) {
            say(HA_LOG_WARN, n == 0 ? "the broker closed the connection" : "receive failed");
            return 0;
        }
        if (!ha_agent_input(a, buf, (size_t)n, ha_plat_now_ms()))
            return 0;
        w = ha_net_wait(s, 0);
    }
    return 1;
}

/* the broker is on this machine, so plain MQTT never crosses a network */
static int loopback(const char *h)
{
    return ha_ieq(h, "localhost") || strncmp(h, "127.", 4) == 0 || strcmp(h, "::1") == 0;
}

static int print_mode(const struct ha_agent_cfg *cfg, const struct ha_identity *id)
{
    static struct ha_sample s1, s2;
    static struct ha_comp_set set;
    static struct ha_track track;
    struct ha_figures f;
    ha_buf b;
    uint64_t t0;
    int i, k;
    ha_plat_sample(&s1);
    t0 = ha_plat_now_ms();
    ha_plat_sleep_ms(2000);
    ha_plat_sample(&s2);
    for (i = k = 0; i < s2.nqueue; i++)
        if (ha_agent_queue_wanted(cfg->queues, s2.queue[i].name))
            s2.queue[k++] = s2.queue[i];
    s2.nqueue = k;
    ha_model_figures(&s1, &s2, ha_plat_now_ms() - t0, &f);
    ha_track_update(&track, &s2, ha_plat_now_ms(), (uint64_t)cfg->error_hold_s * 1000, &f);
    ha_model_components(&s2, &set);
    ha_buf_init(&b);
    if (!ha_model_discovery(&cfg->model, id, &set, NULL, &b))
        return EXIT_FAILURE;
    printf("%s\n", b.p);
    if (!ha_model_state(&set, &s2, &f, &b))
        return EXIT_FAILURE;
    printf("%s\n", b.p);
    ha_buf_free(&b);
    return 0;
}

int main(int argc, char **argv)
{
    static ha_agent agent;              /* large: not on the stack */
    static struct ha_agent_cfg cfg;
    static struct ha_identity id;
    static struct ha_sample sample;
    static char nid[64], dev_name[64], client_id[96], port[24], status_topic[128];
    static char published_path[1100], stop_path[1100];
    ha_conf conf;
    ha_buf file, pw;
    char err[300];
    const char *path, *v, *host, *user, *ca, *dir;
    const char *disc_prefix, *topic_prefix;
    long keepalive, interval, portn, hold;
    int remove_mode = 0, print_only = 0, tls, argi = 1, rc = 0;
    ha_sock *sock = NULL;
    uint64_t retry_at = 0, remove_deadline = 0;

    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("hass_agent %s\n", HA_VERSION);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--random") == 0) {
        /* letters and digits only: a VMS password, never typed by anyone */
        static const char set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        unsigned char r[32];
        char word[33];
        int i;
        if (!ha_net_random(r, sizeof r))
            return EXIT_FAILURE;
        for (i = 0; i < 32; i++)
            word[i] = set[r[i] % 36];         /* 256 % 36 = 4: a tiny bias, harmless here */
        word[32] = 0;
        printf("%s\n", word);
        memset(r, 0, sizeof r);
        memset(word, 0, sizeof word);
        return 0;
    }
    if (argc > argi && strcmp(argv[argi], "--remove") == 0) {
        remove_mode = 1;
        argi++;
    } else if (argc > argi && strcmp(argv[argi], "--print") == 0) {
        print_only = 1;
        argi++;
    }
    path = argc > argi ? argv[argi] : ha_plat_default_config();
    if (!ha_plat_init())
        return EXIT_FAILURE;
    /* D9: before anything else, so a process that must not run the agent
       never reads its settings or touches DATA_DIR */
    if (!remove_mode && !print_only && !ha_plat_check_account(err, sizeof err)) {
        say(HA_LOG_ERROR, "%s", err);
        return HA_EXIT_REFUSED;
    }

    ha_conf_init(&conf, ha_plat_conf_override, NULL);
    ha_buf_init(&file);
    ha_buf_init(&pw);
    if (ha_plat_read_file(path, &file)) {
        if (!ha_conf_parse(&conf, (const char *)file.p, file.len, err, sizeof err)) {
            say(HA_LOG_ERROR, "%s: %s", path, err);
            { rc = EXIT_FAILURE; goto out; }
        }
    } else if (argc > argi) {
        say(HA_LOG_ERROR, "cannot read the configuration file %s", path);
        { rc = EXIT_FAILURE; goto out; }
    }

    v = ha_conf_get(&conf, "LOG_LEVEL");
    if (v && *v) {
        if (ha_ieq(v, "debug")) min_level = HA_LOG_DEBUG;
        else if (ha_ieq(v, "info")) min_level = HA_LOG_INFO;
        else if (ha_ieq(v, "warn")) min_level = HA_LOG_WARN;
        else if (ha_ieq(v, "error")) min_level = HA_LOG_ERROR;
        else {
            say(HA_LOG_ERROR, "LOG_LEVEL must be debug, info, warn or error");
            { rc = EXIT_FAILURE; goto out; }
        }
    }

    /* the strings must outlive the agent: keep copies, not conf_get's scratch */
    {
        static char s_host[256], s_user[128], s_ca[1024], s_dir[1024], s_disc[64], s_topic[64];
        static char s_nodeid[64], s_devname[64], s_queues[1024];
        const char *t;
        t = ha_conf_get(&conf, "MQTT_HOST");   ha_strlcpy(s_host, t ? t : "", sizeof s_host);
        t = ha_conf_get(&conf, "MQTT_USER");   ha_strlcpy(s_user, t ? t : "", sizeof s_user);
        t = ha_conf_get(&conf, "MQTT_CA_FILE"); ha_strlcpy(s_ca, t ? t : "", sizeof s_ca);
        t = ha_conf_get(&conf, "DATA_DIR");
        ha_strlcpy(s_dir, t && *t ? t : ha_plat_default_data_dir(), sizeof s_dir);
        t = ha_conf_get(&conf, "DISCOVERY_PREFIX");
        ha_strlcpy(s_disc, t && *t ? t : "homeassistant", sizeof s_disc);
        t = ha_conf_get(&conf, "TOPIC_PREFIX");
        ha_strlcpy(s_topic, t && *t ? t : "openvms", sizeof s_topic);
        t = ha_conf_get(&conf, "NODE_ID");     ha_strlcpy(s_nodeid, t ? t : "", sizeof s_nodeid);
        t = ha_conf_get(&conf, "DEVICE_NAME"); ha_strlcpy(s_devname, t ? t : "", sizeof s_devname);
        t = ha_conf_get(&conf, "QUEUES");
        ha_strlcpy(s_queues, t && *t ? t : "*", sizeof s_queues);
        cfg.queues = s_queues;
        host = s_host;
        user = s_user;
        ca = s_ca;
        dir = s_dir;
        disc_prefix = s_disc;
        topic_prefix = s_topic;
        if (!number(ha_conf_get(&conf, "MQTT_PORT"), 1, 65535, -1, "MQTT_PORT", &portn) ||
            !number(ha_conf_get(&conf, "MQTT_KEEPALIVE"), 10, 3600, 60, "MQTT_KEEPALIVE", &keepalive) ||
            !number(ha_conf_get(&conf, "INTERVAL"), 5, 3600, 30, "INTERVAL", &interval) ||
            !number(ha_conf_get(&conf, "ERROR_HOLD"), 1, 10080, 60, "ERROR_HOLD", &hold))
            { rc = EXIT_FAILURE; goto out; }
        tls = yes(ha_conf_get(&conf, "MQTT_TLS"));
        if (portn < 0)
            portn = tls ? 8883 : 1883;
        snprintf(port, sizeof port, "%ld", portn);

        ha_plat_identity(&id);
        if (!node_id(nid, sizeof nid, s_nodeid, dir, &id, !print_only))
            { rc = EXIT_FAILURE; goto out; }
        ha_strlcpy(dev_name, *s_devname ? s_devname : id.node, sizeof dev_name);

        if (!print_only) {
            if (!*host) {
                say(HA_LOG_ERROR, "MQTT_HOST is not set (in %s or HASS_AGENT_MQTT_HOST)", path);
                { rc = EXIT_FAILURE; goto out; }
            }
            if (*user) {
                t = ha_conf_get(&conf, "MQTT_PASSWORD_FILE");
                if (!t || !*t) {
                    say(HA_LOG_ERROR, "MQTT_USER is set, so MQTT_PASSWORD_FILE must be too");
                    { rc = EXIT_FAILURE; goto out; }
                }
                if (ha_plat_file_exposed(t)) {
                    say(HA_LOG_ERROR, "%s can be read by others: allow the owner only", t);
                    { rc = EXIT_FAILURE; goto out; }
                }
                if (!ha_plat_read_file(t, &file) ||
                    !ha_conf_secret_file((const char *)file.p, file.len, &pw) || !pw.len) {
                    say(HA_LOG_ERROR, "cannot read one password line from %s", t);
                    wipe(&file);
                    { rc = EXIT_FAILURE; goto out; }
                }
                wipe(&file);
            }
        }
    }

    snprintf(client_id, sizeof client_id, "vms-hass-%s", nid);
    snprintf(status_topic, sizeof status_topic, "%s/status", disc_prefix);
    cfg.model.discovery_prefix = disc_prefix;
    cfg.model.topic_prefix = topic_prefix;
    cfg.model.node_id = nid;
    cfg.model.device_name = dev_name;
    cfg.model.agent_version = HA_VERSION;
    /* HA marks the sensors unavailable if three intervals pass without a state */
    cfg.model.expire_after_s = (int)interval * 3 + 10;
    cfg.sess.client_id = client_id;
    cfg.sess.user = *user ? user : NULL;
    cfg.sess.password = *user ? (const char *)pw.p : NULL;
    cfg.sess.keepalive_s = (unsigned)keepalive;
    cfg.sess.status_topic = status_topic;
    cfg.interval_s = (unsigned)interval;
    cfg.error_hold_s = (unsigned)hold * 60;

    if (print_only)
        { rc = print_mode(&cfg, &id); goto out; }

    ha_plat_data_path(stop_path, sizeof stop_path, dir, "stop");
    if (!remove_mode && ha_plat_exists(stop_path)) {
        say(HA_LOG_INFO, "removing an old stop request %s", stop_path);
        ha_plat_remove(stop_path);
    }

    ha_agent_init(&agent, &cfg, &id, log_cb, NULL, ha_plat_now_ms());
    ha_plat_data_path(published_path, sizeof published_path, dir, "published");
    if (ha_plat_read_file(published_path, &file))
        ha_agent_load_published(&agent, (const char *)file.p, file.len);
    say(HA_LOG_INFO, "hass_agent %s: node %s as \"%s\", broker %s:%s%s, every %ld s",
        HA_VERSION, nid, dev_name, host, port, tls ? " (TLS)" : "", interval);
    if (!tls && !loopback(host))        /* D10 */
        say(HA_LOG_WARN, "MQTT_TLS is off: %s and every reading cross the network unencrypted",
            *user ? "the broker password" : "the broker connection");

    while (!ha_plat_stop_requested()) {
        uint64_t now = ha_plat_now_ms(), wait;
        if (!remove_mode && ha_plat_exists(stop_path)) {
            /* left in place: HASS$RUN.COM sees it too, and doesn't restart us */
            say(HA_LOG_INFO, "stop requested (%s)", stop_path);
            break;
        }
        ha_plat_poll();
        if (!sock && now >= retry_at) {
            sock = ha_net_open(host, port, tls, *ca ? ca : NULL, CONNECT_TIMEOUT_MS,
                               err, sizeof err);
            now = ha_plat_now_ms();
            if (!sock || !ha_agent_begin(&agent, now)) {
                if (sock)
                    ha_net_close(sock);
                else
                    snprintf(agent.sess.err, sizeof agent.sess.err, "%.150s", err);
                sock = NULL;
                ha_agent_closed(&agent, now);
                retry_at = now + ha_agent_backoff_ms(&agent, ha_plat_random());
                say(HA_LOG_INFO, "retrying in %llu s",
                    (unsigned long long)((retry_at - now + 500) / 1000));
                if (remove_mode) {
                    rc = EXIT_FAILURE;
                    break;
                }
                continue;
            }
            if (remove_mode)
                remove_deadline = now + 20000;
        }
        if (!remove_mode && ha_agent_sample_due(&agent, now)) {
            if (ha_plat_sample(&sample)) {
                if (!ha_agent_sample(&agent, &sample, now) && sock)
                    goto drop;
            } else {
                say(HA_LOG_WARN, "sampling the system failed");
            }
        }
        if (sock) {
            if (!ha_agent_tick(&agent, ha_plat_now_ms()) || !flush(&agent, sock))
                goto drop;
            if (remove_mode) {
                static int removed;
                if (ha_agent_up(&agent) && !removed) {
                    if (!ha_agent_remove(&agent, ha_plat_now_ms()) || !flush(&agent, sock))
                        goto drop;
                    removed = 1;
                }
                if (removed && !ha_agent_pending(&agent)) {
                    say(HA_LOG_INFO, "device %s removed from Home Assistant", nid);
                    ha_agent_end(&agent);
                    flush(&agent, sock);
                    break;
                }
                if (ha_plat_now_ms() >= remove_deadline) {
                    say(HA_LOG_ERROR, "the broker did not confirm the removal");
                    rc = EXIT_FAILURE;
                    break;
                }
            }
            now = ha_plat_now_ms();
            wait = ha_agent_wake_in(&agent, now);
            if (wait > WAIT_SLICE_MS)
                wait = WAIT_SLICE_MS;
            if (!pump(&agent, sock, (unsigned)wait) || !flush(&agent, sock))
                goto drop;
            if (ha_agent_published_changed(&agent)) {
                ha_agent_published_text(&agent, &file);
                if (!ha_plat_write_file(published_path, file.p ? file.p : (unsigned char *)"",
                                        file.len))
                    say(HA_LOG_WARN, "cannot save the sensor list to %s", published_path);
            }
            continue;
drop:
            ha_net_close(sock);
            sock = NULL;
            now = ha_plat_now_ms();
            ha_agent_closed(&agent, now);
            if (remove_mode) {
                rc = EXIT_FAILURE;
                break;
            }
            retry_at = now + ha_agent_backoff_ms(&agent, ha_plat_random());
            say(HA_LOG_INFO, "reconnecting in %llu s",
                (unsigned long long)((retry_at - now + 500) / 1000));
        } else {
            wait = ha_agent_wake_in(&agent, now);
            if (retry_at > now && retry_at - now < wait)
                wait = retry_at - now;
            if (wait > WAIT_SLICE_MS)
                wait = WAIT_SLICE_MS;
            if (wait)
                ha_plat_sleep_ms((unsigned)wait);
        }
    }

    if (sock) {
        if (!remove_mode) {
            say(HA_LOG_INFO, "stopping: publishing offline");
            ha_agent_stop(&agent, ha_plat_now_ms());
        }
        flush(&agent, sock);
        ha_net_close(sock);
    }
    ha_agent_free(&agent);
out:
    wipe(&file);
    wipe(&pw);
    ha_conf_free(&conf);
    return rc;
}
