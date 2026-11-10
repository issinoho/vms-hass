$! vax_cc.com - compile vax_cc.c once per feature, report which compile and link.
$!   vax.py run vax_cc.com <work-dir>   (vax_cc.c uploaded there first)
$ set noon
$ set default 'p1'
$ tests = "BASE,STDINT,INTTYPES,LONGLONG,INT64,QUAD,STDBOOL,COMMENT,MIXED,INLINE,DESIG," + -
          "COMPOUND,VARIADIC,FLEX,RESTRICT,FUNC,SNPRINTF,VSNPRINTF,STRTOUL,ISNAN," + -
          "SOCKETS,SOCKETS_BSD,GETADDRINFO,RMI"
$ i = 0
$loop:
$ t = f$element(i, ",", tests)
$ if t .eqs. "," then goto done
$ i = i + 1
$ define/user sys$error nl:
$ define/user sys$output nl:
$ cc/prefix=all/define=(T_'t')/object=vax_cc.obj vax_cc.c
$ csev = $severity
$ lsev = 0
$ if csev .eq. 1 .or. csev .eq. 3
$ then
$   define/user sys$error nl:
$   define/user sys$output nl:
$   link/exe=vax_cc.exe vax_cc.obj
$   lsev = $severity
$ endif
$ write sys$output "PROBE ", t, " cc=", csev, " link=", lsev
$ goto loop
$done:
$ write sys$output "--- C99 qualifier:"
$ cc/standard=c99 vax_cc.c /object=vax_cc.obj
$ write sys$output "--- base run:"
$ cc/prefix=all/define=T_BASE vax_cc.c /object=vax_cc.obj
$ link/exe=vax_cc.exe vax_cc.obj
$ run vax_cc.exe
$ write sys$output "--- float qualifier default (G vs D):"
$ cc/float=g_float/define=T_BASE vax_cc.c /object=vax_cc.obj
$ write sys$output "g_float sev ", $severity
$ write sys$output "--- int64 errors:"
$ cc/define=T_INT64 vax_cc.c /object=vax_cc.obj
$ cc/define=T_LONGLONG vax_cc.c /object=vax_cc.obj
$ cc/define=T_STDINT vax_cc.c /object=vax_cc.obj
$ delete vax_cc.obj;*,vax_cc.exe;*
