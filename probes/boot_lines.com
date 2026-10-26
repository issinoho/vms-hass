$! boot_lines.com - add the vms-hass agent to this node's boot files (asked for
$! by the user, 2026-10-26).  Writes a new version of each file, so the previous
$! version stays as the backup, and gives it back to SYSTEM.  Does nothing to a
$! file that already mentions HASS$.
$!   @boot_lines <st-number>   e.g. 56: adds st_<n> after the st_<n-1> line
$ set noon
$ say = "write sys$output"
$ define sys$error sys$output
$ x = f$setprv("SYSPRV,BYPASS")
$ n = f$integer(p1)
$ prev = "st_" + f$string(n - 1)
$!
$ ! SYS$MANAGER is a search list (SYS$SPECIFIC, then SYS$COMMON): write the
$ ! new version where the file is, so it never shadows the real one
$ f = f$search("SYS$MANAGER:SYSTARTUP_VMS.COM") - f$parse(f$search("SYS$MANAGER:SYSTARTUP_VMS.COM"),,,"VERSION")
$ search/nooutput 'f' "HASS$"
$ if $severity .eq. 1
$ then
$   say "BL: ", f, " already mentions HASS$: unchanged"
$   goto shutdown
$ endif
$ search/nooutput 'f' "st_''n' "
$ if $severity .eq. 1
$ then
$   say "BL: ", f, " already has st_''n': not touching it"
$   exit 44
$ endif
$ open/read i 'f'
$ open/write o 'f'
$ done = 0
$st_loop:
$ read/end=st_end i line
$ write o line
$ if .not. done .and. f$extract(0, f$length(prev) + 3, f$edit(line, "COMPRESS")) .eqs. "$ ''prev' "
$ then
$   write o "$ st_''n' = ""Home Assistant agent|SYS$STARTUP:HASS$STARTUP.COM"""
$   done = 1
$ endif
$ goto st_loop
$st_end:
$ close i
$ close o
$ if .not. done
$ then
$   say "BL: no ''prev' line in ", f, ": removing the new version"
$   delete/nolog 'f';0
$   exit 44
$ endif
$ set file/owner=SYSTEM 'f';0
$ say "BL: added st_''n' to ", f
$!
$shutdown:
$ f = f$search("SYS$MANAGER:SYSHUTDWN.COM") - f$parse(f$search("SYS$MANAGER:SYSHUTDWN.COM"),,,"VERSION")
$ search/nooutput 'f' "HASS$"
$ if $severity .eq. 1
$ then
$   say "BL: ", f, " already mentions HASS$: unchanged"
$   goto show
$ endif
$ open/read i 'f'
$ open/write o 'f'
$sd_loop:
$ read/end=sd_end i line
$ ! IA64's beszel line had a typo, so its shutdown never ran (the user: fix it)
$ if f$locate("BESZEL$SHTDOWN", f$edit(line, "UPCASE")) .lt. f$length(line)
$ then
$   line = "$ @SYS$STARTUP:BESZEL$SHUTDOWN.COM"
$   say "BL: fixed BESZEL$SHTDOWN -> BESZEL$SHUTDOWN in ", f
$ endif
$ write o line
$ goto sd_loop
$sd_end:
$ write o "$ @SYS$STARTUP:HASS$SHUTDOWN.COM"
$ close i
$ close o
$ set file/owner=SYSTEM 'f';0
$ say "BL: added HASS$SHUTDOWN to ", f
$!
$show:
$ directory/owner/protection/date SYS$SPECIFIC:[SYSMGR]SYSTARTUP_VMS.COM;*,SYSHUTDWN.COM;*
$ directory/owner/protection/date SYS$COMMON:[SYSMGR]SYSTARTUP_VMS.COM;*,SYSHUTDWN.COM;*
$ s = f$search("SYS$MANAGER:SYSTARTUP_VMS.COM")
$ differences/output=sys$output 's' 's';-1
$ s = f$search("SYS$MANAGER:SYSHUTDWN.COM")
$ differences/output=sys$output 's' 's';-1
$ exit
