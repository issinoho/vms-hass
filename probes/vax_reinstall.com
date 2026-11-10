$! vax_reinstall.com - remove the HASS kit on the VAX and install it again from
$! [.KIT_VAX] (same version: VAX PCSI has no /OPTIONS, so the answers follow each
$! command).  Checks that removal keeps CONF, DATA and LOG and the node ID, then
$! starts the agent as a boot would.  Run as SYSTEM in the work directory:
$!   vax.sh run vax_reinstall.com
$ set noon
$ say = "write sys$output"
$ define sys$error sys$output
$ here = f$environment("default")
$ kitdir = here - "]" + ".KIT_VAX]"
$ say "=== REMOVE"
$ define/user_mode sys$command sys$input
$ product remove HASS /producer=ISSINOHO /log
YES
$ say "remove status ", $status
$ say "HASS$ROOT after removal: [", f$trnlnm("HASS$ROOT"), "]  HASS$USER: [", f$trnlnm("HASS$USER"), "]"
$ say "agent process after removal: [", f$search("SYS$COMMON:[HASS.BIN]HASS_AGENT.EXE"), "]"
$ directory/size SYS$COMMON:[HASS.CONF],[HASS.DATA],[HASS.LOG]
$ say "=== INSTALL from ", kitdir
$ define/user_mode sys$command sys$input
$ product install HASS /producer=ISSINOHO /base_system=VMS /source='kitdir' /log
YES
YES
NO
$ say "install status ", $status
$ product show product HASS /producer=ISSINOHO
$ say "=== boot: HASS$STARTUP"
$ @SYS$STARTUP:HASS$STARTUP
$ wait 0:0:30
$ @HASS$ROOT:[COM]HASS$CONTROL STATUS
$ agent := $HASS$ROOT:[BIN]HASS_AGENT.EXE
$ agent --version
$ type HASS$ROOT:[DATA]NODE_ID.
$ exit 1
