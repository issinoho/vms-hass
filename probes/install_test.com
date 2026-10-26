$! install_test.com - Phase 4 install test (system-wide: approved by the user,
$! account HASS [363,1], start at boot YES).  Run in batch, after the hand-run
$! agent's batch job is stopped.
$!   @install_test INSTALL <kit-dir>   install, configure, carry over this node's
$!                                     identity and broker settings, start
$!   @install_test BOOT                stop, forget HASS$ROOT and HASS$USER, and
$!                                     run HASS$STARTUP as a boot would
$!   @install_test REMOVE  <kit-dir>   remove, check what is kept, reinstall, start
$ set noon
$ say = "write sys$output"
$ define sys$error sys$output
$ x = f$setprv("ALL")
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ kitdir = p2
$ op = f$edit(p1, "UPCASE")
$ if op .eqs. "REMOVE" then goto remove
$ if op .eqs. "BOOT" then goto boot
$!
$ say "IT: === before: node ID ", f$search("[.DATA]NODE_ID.")
$ type [.DATA]NODE_ID.
$ say "IT: === PRODUCT INSTALL"
$ product install HASS /producer=ISSINOHO /source='kitdir' /options=noconfirm /log
$ say "IT: install status ", $status
$ say "IT: HASS$ROOT = ", f$trnlnm("HASS$ROOT","LNM$SYSTEM_TABLE")
$ say "IT: === HASS$CONFIGURE (new account)"
$ @HASS$ROOT:[COM]HASS$CONFIGURE HASS [363,1] YES
$ say "IT: configure status ", $status
$ say "IT: === carry over the broker settings, the password and the node's identity"
$ ! AGENT.CONF from the hand-run one, with the password file and DATA_DIR in the tree
$ open/read i AGENT.CONF
$ open/write o HASS$ROOT:[CONF]AGENT.CONF
$ write o "# carried over from the hand-run agent by install_test.com"
$co_loop:
$ read/end=co_done i line
$ key = f$edit(f$element(0, "=", line), "UPCASE,TRIM")
$ if key .eqs. "DATA_DIR" then goto co_loop
$ if key .eqs. "MQTT_PASSWORD_FILE" then line = "MQTT_PASSWORD_FILE=HASS$ROOT:[CONF]MQTT.PWD"
$ write o line
$ goto co_loop
$co_done:
$ close i
$ close o
$ purge/nolog HASS$ROOT:[CONF]AGENT.CONF
$ copy/nolog MQTT.PWD HASS$ROOT:[CONF]MQTT.PWD
$ ! (a tree kept from an earlier install already has them: the same node ID)
$ if f$search("HASS$ROOT:[DATA]NODE_ID.") .eqs. "" then copy/nolog [.DATA]NODE_ID.;0 HASS$ROOT:[DATA]NODE_ID.
$ if f$search("HASS$ROOT:[DATA]PUBLISHED.") .eqs. "" then copy/nolog [.DATA]PUBLISHED.;0 HASS$ROOT:[DATA]PUBLISHED.
$ say "IT: === HASS$CONFIGURE again (the existing account; owners and protections)"
$ @HASS$ROOT:[COM]HASS$CONFIGURE HASS "" YES
$ say "IT: configure status ", $status
$ directory/owner/protection HASS$ROOT:[000000...]
$ type SYS$MANAGER:HASS$CONFIG.COM
$ say "IT: HASS$USER = ", f$trnlnm("HASS$USER","LNM$SYSTEM_TABLE")
$ define/user sysuaf sys$system:sysuaf
$ mcr authorize show HASS
$ say "IT: node ID in the tree:"
$ type HASS$ROOT:[DATA]NODE_ID.
$ say "IT: === START"
$ @HASS$ROOT:[COM]HASS$CONTROL START
$ wait 0:0:40
$ @HASS$ROOT:[COM]HASS$CONTROL STATUS
$ type HASS$ROOT:[LOG]HASS_AGENT.LOG
$ exit
$!
$boot:
$ say "IT: === STOP"
$ @HASS$ROOT:[COM]HASS$CONTROL STOP
$ say "IT: === as at boot: no HASS$ROOT, no HASS$USER"
$ deassign/system/executive_mode HASS$USER
$ deassign/system/executive_mode HASS$ROOT
$ say "IT: HASS$ROOT = """, f$trnlnm("HASS$ROOT"), """, HASS$USER = """, f$trnlnm("HASS$USER"), """"
$ @SYS$STARTUP:HASS$STARTUP
$ say "IT: startup status ", $status
$ say "IT: HASS$ROOT = ", f$trnlnm("HASS$ROOT","LNM$SYSTEM_TABLE"), ", HASS$USER = ", -
      f$trnlnm("HASS$USER","LNM$SYSTEM_TABLE")
$ wait 0:0:40
$ @HASS$ROOT:[COM]HASS$CONTROL STATUS
$ type HASS$ROOT:[LOG]HASS_AGENT.LOG
$ exit
$!
$remove:
$ root = f$trnlnm("HASS$ROOT","LNM$SYSTEM_TABLE")
$ say "IT: === PRODUCT REMOVE (root ", root, ")"
$ product remove HASS /options=noconfirm /log
$ say "IT: remove status ", $status
$ say "IT: HASS$ROOT after remove = """, f$trnlnm("HASS$ROOT","LNM$SYSTEM_TABLE"), """, HASS$USER = """, -
      f$trnlnm("HASS$USER","LNM$SYSTEM_TABLE"), """"
$ say "IT: agent processes after remove (none expected):"
$ pipe show system | search sys$pipe HASS_AGENT
$ dev = f$element(0, "[", root)
$ dir = "[" + (f$element(1, "[", root) - ".]") + "...]"
$ say "IT: what is left in ", dev, dir
$ directory/owner/protection 'dev''dir'
$ say "IT: === reinstall"
$ product install HASS /producer=ISSINOHO /source='kitdir' /options=noconfirm /log
$ @HASS$ROOT:[COM]HASS$CONFIGURE HASS "" YES
$ @HASS$ROOT:[COM]HASS$CONTROL START
$ wait 0:0:40
$ @HASS$ROOT:[COM]HASS$CONTROL STATUS
$ type HASS$ROOT:[LOG]HASS_AGENT.LOG
$ type HASS$ROOT:[DATA]NODE_ID.
$ exit
