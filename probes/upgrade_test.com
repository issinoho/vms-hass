$! upgrade_test.com - install a new kit over the running one and restart the agent
$! (the 0.1.2 release, approved by the user 2026-11-10).  Run in batch in the
$! work directory after tools/kit.sh <node>:
$!   vms.sh <node> batch upgrade_test.com
$! Shows the node ID before and after (it must not change), the installed
$! version, the agent's own version, and its log after the restart.
$ set noon
$ say = "write sys$output"
$ define sys$error sys$output
$ x = f$setprv("ALL")
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ kitdir = "[.KIT_''arch']"
$ say "UT: === before"
$ product show product HASS /producer=ISSINOHO
$ type HASS$ROOT:[DATA]NODE_ID.
$ say "UT: === PRODUCT INSTALL from ", f$search("''kitdir'*.PCSI")
$ product install HASS /producer=ISSINOHO /source='kitdir' /options=noconfirm /log
$ say "UT: install status ", $status
$ product show product HASS /producer=ISSINOHO
$ say "UT: === restart"
$ @HASS$ROOT:[COM]HASS$CONTROL RESTART
$ wait 0:0:40
$ @HASS$ROOT:[COM]HASS$CONTROL STATUS
$ agent := $HASS$ROOT:[BIN]HASS_AGENT.EXE
$ agent --version
$ say "UT: === after"
$ type HASS$ROOT:[DATA]NODE_ID.
$ show logical/system HASS$*
$ type HASS$ROOT:[LOG]HASS_AGENT.LOG
$ exit 1
