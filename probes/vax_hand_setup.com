$! vax_hand_setup.com - a hand-run HASS tree on the VAX (D11), no kit, no system logicals.
$!
$!   vax.sh run vax_hand_setup.com <broker-host> <mqtt-user>
$!
$! In the work directory (after build_vms): makes [.HASS] with [BIN] [COM] [CONF]
$! [DATA] [LOG] as the kit would, copies the images and HASS$CONTROL/HASS$RUN,
$! writes [CONF]AGENT.CONF, and moves in the broker password from MQTT_PWD.TMP.
$! If HASS_ADD.TMP is there (an AUTHORIZE ADD for the account, made on the
$! workstation with a random password, as HASS$CONFIGURE would write it), it is
$! run and erased.  Ownership and protection follow HASS$CONFIGURE: [CONF] [DATA]
$! [LOG] belong to the account and only it (and SYSTEM) can read them.
$! Run as SYSTEM.  HASS$CONTROL then starts the agent from this tree as HASS.
$ set noon
$ say = "write sys$output"
$ account = "HASS"
$ here = f$environment("default")
$ root = here - "]" + ".HASS"
$ if p1 .eqs. "" .or. p2 .eqs. ""
$ then
$   say "usage: vax_hand_setup <broker-host> <mqtt-user>"
$   exit 44
$ endif
$ create/directory 'root']
$ create/directory 'root'.BIN]
$ create/directory 'root'.COM]
$ create/directory 'root'.CONF]
$ create/directory 'root'.DATA]
$ create/directory 'root'.LOG]
$ copy/nolog [.BUILD]HASS_AGENT.EXE,HASS_LAUNCH.EXE 'root'.BIN]
$ copy/nolog [.VMS]HASS$CONTROL.COM,HASS$RUN.COM 'root'.COM]
$ purge/nolog 'root'.BIN],'root'.COM]
$!
$ if f$search("HASS_ADD.TMP") .nes. ""
$ then
$   set file/protection=(s:rwd,o,g,w) HASS_ADD.TMP
$   define/user sysuaf sys$system:sysuaf.dat
$   define/user sys$input HASS_ADD.TMP
$   mcr authorize
$   delete/erase/nolog HASS_ADD.TMP;*
$ endif
$ if f$identifier(account, "NAME_TO_NUMBER") .eq. 0
$ then
$   say "SETUP-FAIL: there is no account ''account'"
$   exit 44
$ endif
$!
$ if f$search("''root'.CONF]AGENT.CONF") .eqs. ""
$ then
$   open/write f 'root'.CONF]AGENT.CONF
$   write f "# hand-run tree on the VAX (vax_hand_setup.com): plain MQTT, D10/D11"
$   write f "MQTT_HOST=''p1'"
$   write f "MQTT_USER=''p2'"
$   write f "MQTT_PASSWORD_FILE=HASS$ROOT:[CONF]MQTT.PWD"
$   close f
$ endif
$ if f$search("MQTT_PWD.TMP") .nes. ""
$ then
$   copy/nolog MQTT_PWD.TMP 'root'.CONF]MQTT.PWD
$   delete/erase/nolog MQTT_PWD.TMP;*
$ endif
$!
$ set file/protection=(s:rwe,o:rwe,g:re,w:re) 'here'HASS.DIR
$ set file/protection=(s:rwe,o:rwe,g:re,w:re) 'root']BIN.DIR,COM.DIR
$ set file/protection=(s:rwed,o:rwed,g:re,w:re) 'root'.BIN]*.*;*,'root'.COM]*.*;*
$ set file/owner='account'/protection=(s:rwe,o:rwe,g,w) 'root']CONF.DIR,DATA.DIR,LOG.DIR
$ set file/owner='account'/protection=(s:rwed,o:rwed,g,w) 'root'.CONF]*.*;*
$ dir/owner/protection 'root'...]
$ say "SETUP-OK"
$ exit 1
