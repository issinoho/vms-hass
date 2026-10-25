$! HASS$SHUTDOWN.COM - stop the vms-hass agent at system shutdown
$!
$! Usage (as SYSTEM, from SYS$MANAGER:SYSHUTDWN.COM):
$!   @SYS$STARTUP:HASS$SHUTDOWN
$!
$! The agent publishes "offline" and disconnects, so Home Assistant marks the
$! node unavailable at once$! instead of after its timeout.
$ set noon
$ if f$trnlnm("HASS$ROOT", "LNM$SYSTEM_TABLE") .eqs. "" then exit 1
$ if f$search("HASS$ROOT:[COM]HASS$CONTROL.COM") .eqs. "" then exit 1
$ @HASS$ROOT:[COM]HASS$CONTROL STOP
$ exit 1
