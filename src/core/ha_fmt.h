/* ha_fmt.h - a bounded snprintf for C libraries that lack one (ha_port.h). */
#ifndef HA_FMT_H
#define HA_FMT_H

#include <stdarg.h>
#include <stddef.h>

/* As C99's: writes at most cap bytes including the NUL, and returns the length
   the whole output would have had.  Conversions: d i u x X o c s f %, with the
   flags - 0 + and space, a width and a precision (numbers or *), and the length
   modifiers l and z.  Anything else is copied as written. */
int ha_vsnprintf(char *out, size_t cap, const char *fmt, va_list ap);
int ha_snprintf(char *out, size_t cap, const char *fmt, ...);

#endif
