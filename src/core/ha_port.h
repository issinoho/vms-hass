/* ha_port.h - the integer types and formatting the core relies on (DECISIONS.md D11).
 *
 * The core never names uint64_t or int64_t: it uses ha_u64 and ha_i64.  They are
 * the C99 types everywhere except where HA_WIDE_DOUBLE is defined, where they are
 * double.  That is OpenVMS VAX, whose C compiler has no 64-bit integer type at all
 * (NOTES.md 15); its D_float double holds every integer up to 2^56 exactly, which
 * covers millisecond clocks, byte counts and tick counters.  Only +, -, * and
 * comparisons may be used on them: no %, no integer division, no shifts, and they
 * are printed after HA_TO_ULONG.  The Linux build runs the unit tests
 * a second time with HA_WIDE_DOUBLE (make test-wide) to keep it that way.
 *
 * HA_NO_SNPRINTF (VAX: its C library has neither snprintf nor vsnprintf) maps both
 * to ha_snprintf/ha_vsnprintf (ha_fmt.c), which the core always builds and the
 * unit tests compare with the C library's.
 */
#ifndef HA_PORT_H
#define HA_PORT_H

#ifdef __VAX
#  ifndef HA_WIDE_DOUBLE
#    define HA_WIDE_DOUBLE 1
#  endif
#  ifndef HA_NO_SNPRINTF
#    define HA_NO_SNPRINTF 1
#  endif
#  ifndef HA_NO_TLS
#    define HA_NO_TLS 1
#  endif
typedef unsigned int uint32_t;
#else
#  include <stdint.h>
#endif

#ifdef HA_WIDE_DOUBLE
typedef double ha_u64;
typedef double ha_i64;
#else
typedef uint64_t ha_u64;
typedef int64_t ha_i64;
#endif

/* a whole number of ha_u64/ha_i64 units for printf: (unsigned long) is enough
   for the seconds and counts the agent logs, and exists everywhere */
#define HA_TO_ULONG(v) ((unsigned long)(v))

#include "ha_fmt.h"

#ifdef HA_NO_SNPRINTF
#  undef snprintf
#  undef vsnprintf
#  define snprintf ha_snprintf
#  define vsnprintf ha_vsnprintf
#endif

#endif
