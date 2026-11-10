$! vax_install.com - install the HASS kit on the VAX and move the hand-run agent
$! onto it (D11; asked for by the user 2026-11-10).  Run as SYSTEM in the work
$! directory, after tools/kit.sh vax:
$!   vax.sh run vax_install.com
$! 1. stops the hand-run agent ([.HASS], vax_hand_setup.com)
$! 2. PRODUCT INSTALL HASS from [.KIT_VAX] (VAX PCSI has no /OPTIONS: answers below)
$! 3. carries AGENT.CONF, MQTT.PWD, NODE_ID and PUBLISHED over, as the Phase 4
$!    install test did, so the node ID and HA's device stay the same
$! 4. HASS$CONFIGURE HASS (the account exists and is ours) with start at boot
$! 5. HASS$CONTROL START, then shows where things are
$ set noon
$ say = "write sys$output"
$ define sys$error sys$output
$ here = f$environment("default")
$ hand = here - "]" + ".HASS"
$ kitdir = here - "]" + ".KIT_VAX]"
$ say "=== 1. stop the hand-run agent"
$ if f$search("''hand'.COM]HASS$CONTROL.COM") .nes. "" then @'hand'.COM]HASS$CONTROL STOP HASS
$!
$ say "=== 2. install from ", kitdir
$ define/user_mode sys$command sys$input
$ product install HASS /producer=ISSINOHO /base_system=VMS /source='kitdir' /log
YES
YES
NO
$ say "install status ", $status
$ product show product HASS /producer=ISSINOHO
$ if f$trnlnm("HASS$ROOT", "LNM$SYSTEM_TABLE") .eqs. ""
$ then
$   say "INSTALL-FAIL: HASS$ROOT is not defined"
$   exit 44
$ endif
$ show logical HASS$ROOT
$!
$ say "=== 3. carry over the hand-run tree's settings and identity"
$ if f$search("HASS$ROOT:[CONF]AGENT.CONF") .eqs. "" .and. f$search("''hand'.CONF]AGENT.CONF") .nes. "" then -
     copy/nolog 'hand'.CONF]AGENT.CONF HASS$ROOT:[CONF]AGENT.CONF
$ if f$search("HASS$ROOT:[CONF]MQTT.PWD") .eqs. "" .and. f$search("''hand'.CONF]MQTT.PWD") .nes. "" then -
     copy/nolog 'hand'.CONF]MQTT.PWD HASS$ROOT:[CONF]MQTT.PWD
$ if f$search("HASS$ROOT:[DATA]NODE_ID.") .eqs. "" .and. f$search("''hand'.DATA]NODE_ID.") .nes. "" then -
     copy/nolog 'hand'.DATA]NODE_ID. HASS$ROOT:[DATA]NODE_ID.
$ if f$search("HASS$ROOT:[DATA]PUBLISHED.") .eqs. "" .and. f$search("''hand'.DATA]PUBLISHED.") .nes. "" then -
     copy/nolog 'hand'.DATA]PUBLISHED. HASS$ROOT:[DATA]PUBLISHED.
$!
$ say "=== 4. configure"
$ @HASS$ROOT:[COM]HASS$CONFIGURE HASS "" YES
$ say "configure status ", $status
$ type SYS$MANAGER:HASS$CONFIG.COM
$ show logical/system HASS$USER
$!
$ say "=== 5. start"
$ @HASS$ROOT:[COM]HASS$CONTROL START
$ wait 0:0:30
$ @HASS$ROOT:[COM]HASS$CONTROL STATUS
$ directory/owner/protection HASS$ROOT:[000000...]
$ define/user sysuaf sys$system:sysuaf.dat
$ mcr authorize show HASS
$ exit 1
