/* boot_probe.c - does mktime() turn SYI$_BOOTTIME (local) into the right UTC,
   with the DST rule for the boot date rather than today's offset? */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <starlet.h>
#include <syidef.h>

static long long utc_of_local(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s; t.tm_isdst = -1;
    return (long long)mktime(&t);
}

static void show(const char *what, long long u)
{
    time_t t = (time_t)u;
    char b[32];
    strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    printf("%-28s %s\n", what, b);
}

int main(void)
{
    unsigned __int64 boot = 0, now;
    unsigned short n[7];
    struct { unsigned short len, code; void *buf; void *ret; } il[2];
    unsigned short iosb[4];
    int st;

    il[0].len = 8; il[0].code = SYI$_BOOTTIME; il[0].buf = &boot; il[0].ret = 0;
    memset(&il[1], 0, sizeof il[1]);
    st = sys$getsyiw(0, 0, 0, il, iosb, 0, 0);
    printf("getsyi %d iosb %d\n", st, iosb[0]);
    sys$numtim(n, &boot);
    printf("BOOTTIME local %04u-%02u-%02u %02u:%02u:%02u.%02u\n",
           n[0], n[1], n[2], n[3], n[4], n[5], n[6]);
    show("new: mktime(BOOTTIME)", utc_of_local(n[0], n[1], n[2], n[3], n[4], n[5]));
    sys$gettim(&now);
    show("old: time() - local delta", (long long)time(NULL) - (long long)((now - boot) / 10000000));
    show("now: time()", (long long)time(NULL));
    show("2026-07-01 12:00 local", utc_of_local(2026, 7, 1, 12, 0, 0));
    show("2026-10-17 23:18:20 local", utc_of_local(2026, 10, 17, 23, 18, 20));
    show("2026-10-25 12:00 local", utc_of_local(2026, 10, 25, 12, 0, 0));
    show("2026-12-01 12:00 local", utc_of_local(2026, 12, 1, 12, 0, 0));
    return 0;
}
