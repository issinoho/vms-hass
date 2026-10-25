$! HASS$STARTUP.COM - the vms-hass agent for OpenVMS at system startup
$!
$! Usage (as SYSTEM):
$!   @SYS$STARTUP:HASS$STARTUP          from SYSTARTUP_VMS.COM: define HASS$ROOT
$!                                      and HASS$USER, and start the agent if
$!                                      SYS$MANAGER:HASS$CONFIG.COM says AUTOSTART
$!   @SYS$STARTUP:HASS$STARTUP INSTALL  define HASS$ROOT only (PCSI postinstall)
$!   @SYS$STARTUP:HASS$STARTUP REMOVE   stop the agent, deassign (PCSI remove)
$!
$! The agent runs as its own unprivileged account (DECISIONS.md D9); this
$! procedure needs the privileges of a system startup to start it as that account.
$!
$ set noon
$ op = f$edit(p1, "UPCASE,TRIM")
$ if op .eqs. "REMOVE" then goto remove
$! This procedure sits in <destination>[SYS$STARTUP]; the product is in
$! <destination>[HASS].
$ proc = f$environment("PROCEDURE")
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "HASS.]"
$ if root .eqs. dir + "HASS.]"
$ then
$   write sys$error "HASS$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = dev + (root - ".000000")
$ define/system/executive_mode/translation_attributes=concealed HASS$ROOT 'root'
$ if f$search("HASS$ROOT:[BIN]HASS_AGENT.EXE") .eqs. ""
$ then
$   write sys$error "HASS$STARTUP: no HASS$ROOT:[BIN]HASS_AGENT.EXE (root ", root, ")"
$   exit 44
$ endif
$ if op .eqs. "INSTALL" then exit 1
$!
$! boot: the site settings name the account, and say whether to start
$ if f$search("SYS$MANAGER:HASS$CONFIG.COM") .eqs. "" then exit 1
$! Global (==), like the assignments in HASS$CONFIG.COM: a local symbol of
$! the same name would hide them, and AUTOSTART would always read NO (vms-beszel NOTES.md 46)
$ hass_account == ""
$ hass_autostart == "NO"
$ @SYS$MANAGER:HASS$CONFIG.COM
$ if hass_account .nes. "" then -
     define/system/executive_mode HASS$USER 'f$edit(hass_account, "UPCASE,TRIM")'
$ if f$edit(hass_autostart, "UPCASE,TRIM") .nes. "YES"
$ then
$   write sys$output "HASS$STARTUP: start at boot is off (SYS$MANAGER:HASS$CONFIG.COM)"
$   exit 1
$ endif
$ @HASS$ROOT:[COM]HASS$CONTROL START
$ exit 1
$!
$remove:
$ if f$trnlnm("HASS$ROOT", "LNM$SYSTEM_TABLE") .nes. ""
$ then
$   if f$search("HASS$ROOT:[COM]HASS$CONTROL.COM") .nes. "" then -
       @HASS$ROOT:[COM]HASS$CONTROL STOP
$   deassign/system/executive_mode HASS$ROOT
$ endif
$ if f$trnlnm("HASS$USER", "LNM$SYSTEM_TABLE") .nes. "" then -
     deassign/system/executive_mode HASS$USER
$ exit 1
