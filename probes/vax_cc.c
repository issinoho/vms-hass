/* vax_cc.c - which C99 and library features Compaq C 6.4 for VAX has.
   Compiled once per feature by vax_cc.com with /DEFINE=T_<name>. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <time.h>
#if defined T_STDINT
#include <stdint.h>
uint32_t a = 1; uint64_t b = 2; int64_t c = -3;
#elif defined T_INTTYPES
#include <inttypes.h>
uint64_t b = 2;
#elif defined T_LONGLONG
long long b = 2; unsigned long long c = 3;
#elif defined T_INT64
__int64 b = 2; unsigned __int64 c = 3;
#elif defined T_QUAD
#include <gen64def.h>
GENERIC_64 g;
#elif defined T_STDBOOL
#include <stdbool.h>
bool b = true;
#elif defined T_COMMENT
int b = 1; // a C99 comment
#elif defined T_MIXED
int f(void) { int a = 1; a++; int b = a; for (int i = 0; i < 2; i++) b += i; return b; }
#elif defined T_INLINE
static inline int f(int x) { return x + 1; }
#elif defined T_DESIG
struct s { int a, b; }; struct s v = { .b = 2, .a = 1 }; int arr[4] = { [2] = 5 };
#elif defined T_COMPOUND
struct s { int a, b; }; int f(void) { struct s v; v = (struct s){ 1, 2 }; return v.a; }
#elif defined T_VARIADIC
#define LOG(...) printf(__VA_ARGS__)
void f(void) { LOG("%d\n", 1); }
#elif defined T_FLEX
struct s { int n; char d[]; };
#elif defined T_RESTRICT
void f(char *restrict p) { *p = 0; }
#elif defined T_FUNC
const char *f(void) { return __func__; }
#elif defined T_SNPRINTF
int f(char *p, size_t n) { return snprintf(p, n, "%d", 42); }
#elif defined T_VSNPRINTF
int f(char *p, size_t n, const char *fmt, va_list ap) { return vsnprintf(p, n, fmt, ap); }
#elif defined T_STRTOUL
unsigned long f(const char *s) { return strtoul(s, 0, 10); }
#elif defined T_ISNAN
int f(double x) { return isnan(x); }
#elif defined T_SOCKETS
#include <types.h>
#include <socket.h>
#include <in.h>
#include <netdb.h>
#include <inet.h>
int f(void) { return socket(AF_INET, SOCK_STREAM, 0); }
#elif defined T_SOCKETS_BSD
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <unistd.h>
int f(void) { fd_set r; FD_ZERO(&r); return socket(AF_INET, SOCK_STREAM, 0); }
#elif defined T_GETADDRINFO
#include <netdb.h>
int f(void) { struct addrinfo *r; return getaddrinfo("x", "1883", 0, &r); }
#elif defined T_RMI
#include <starlet.h>
#include <rmidef.h>
int f(void) { return RMI$_MODES + RMI$_FRLIST + RMI$_MODLIST; }
#endif
int main(void)
{
    printf("int %d long %d ptr %d double %d time_t %d ULONG_MAX %lu DBL_MAX %g DBL_DIG %d\n",
           (int) sizeof(int), (int) sizeof(long), (int) sizeof(void *),
           (int) sizeof(double), (int) sizeof(time_t), ULONG_MAX, DBL_MAX, DBL_DIG);
    printf("2^53+1 as double: %.0f  4294967296.0*3: %.0f\n", 9007199254740993.0, 4294967296.0 * 3);
    return 1;
}
