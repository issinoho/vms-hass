$! HASS$CONFIGURE.COM - set up the vms-hass agent (run once, as SYSTEM; safe to run again)
$!
$! Usage:  @HASS$ROOT:[COM]HASS$CONFIGURE [account] [uic] [autostart]
$!   account    the agent's account (default HASS)
$!   uic        its UIC, e.g. [377,1]: a group of its own, above MAXSYSGROUP.
$!              Needed only when the account is new.
$!   autostart  YES: HASS$STARTUP starts the agent at boot (default YES)
$! Missing values are asked for when run interactively.
$!
$! What it does (README.VMS; DECISIONS.md D9):
$!  - the account.  A new one gets NETMBX and TMPMBX only, no login class at all
$!    (local, dial-up, remote, network, batch), no DISUSER flag (OpenVMS won't
$!    start processes as a disusered account), LGICMD NL:, DISCTLY and LOCKPWD,
$!    and a random password nobody needs.  An existing account is only changed
$!    if this procedure created it (its owner field says "vms-hass agent"); its
$!    privileges and logins are then set as above again.  A system-group UIC is
$!    refused.
$!  - the tree: [CONF], [DATA] and [LOG] belong to the account and only it can
$!    read them; [BIN], [COM], [DOC] are readable by all.
$!  - HASS$ROOT:[CONF]AGENT.CONF from AGENT.CONF_TEMPLATE, if there is none.
$!  - SYS$MANAGER:HASS$CONFIG.COM (the account; start at boot or not), and the
$!    system logical name HASS$USER.
$!
$ set noon
$ set process/parse_style=extended
$ say = "write sys$output"
$ interactive = f$mode() .eqs. "INTERACTIVE"
$ if f$trnlnm("HASS$ROOT", "LNM$SYSTEM_TABLE") .eqs. ""
$ then
$   say "HASS$CONFIGURE: HASS$ROOT is not defined (@SYS$STARTUP:HASS$STARTUP)"
$   exit 44
$ endif
$ if .not. f$privilege("SYSPRV,CMKRNL")
$ then
$   say "HASS$CONFIGURE: needs SYSPRV and CMKRNL (run it as SYSTEM)"
$   exit 36
$ endif
$!
$ hass_account == ""
$ hass_autostart == ""
$ if f$search("SYS$MANAGER:HASS$CONFIG.COM") .nes. "" then @SYS$MANAGER:HASS$CONFIG.COM
$ accdef = "HASS"
$ if hass_account .nes. "" then accdef = hass_account
$ autodef = "YES"
$ if hass_autostart .nes. "" then autodef = hass_autostart
$ account = f$edit(p1, "UPCASE,TRIM")
$ if account .eqs. "" .and. interactive then inquire account "The agent's account [''accdef']"
$ account = f$edit(account, "UPCASE,TRIM")
$ if account .eqs. "" then account = accdef
$ maxsys = f$getsyi("MAXSYSGROUP")
$ uicnum = f$identifier(account, "NAME_TO_NUMBER")
$!
$ ! a work file for AUTHORIZE: in [CONF], which only SYSTEM and the account can read
$ work = "HASS$ROOT:[CONF]HASS$CONFIGURE.TMP"
$ uaf = f$parse("SYS$SYSTEM:SYSUAF.DAT")
$ if uicnum .eq. 0 then goto new_account
$!
$! ---- an existing account: change it only if we made it ----
$ if uicnum / 65536 .le. maxsys
$ then
$   say "HASS$CONFIGURE: ''account' has a system UIC; the agent must have its own account (D9)"
$   exit 44
$ endif
$ define/user sys$output 'work'
$ define/user sysuaf 'uaf'
$ mcr authorize show 'account'
$ owner = ""
$ open/read f 'work'
$read_owner:
$ read/end=owner_done f line
$ if f$locate("Owner:", line) .lt. f$length(line) then -
     owner = f$edit(f$extract(f$locate("Owner:", line) + 6, 999, line), "TRIM,COMPRESS")
$ goto read_owner
$owner_done:
$ close f
$ delete/nolog 'work';*
$ if f$edit(owner, "UPCASE") .nes. "VMS-HASS AGENT"
$ then
$   say "HASS$CONFIGURE: an account ''account' exists, but this procedure didn't create it"
$   say "  (its owner field is """, owner, """). Choose another name, so a real user's"
$   say "  account isn't changed."
$   exit 44
$ endif
$ say "Account ''account' exists (", f$fao("!%U", uicnum), "); setting its privileges and logins again"
$ open/write f 'work'
$ write f "modify ''account' /privileges=(netmbx,tmpmbx) /defprivileges=(netmbx,tmpmbx) -"
$ write f "  /flags=(nodisuser,disctly,lockpwd,norestricted) /lgicmd=""NL:"" -"
$ write f "  /nolocal /nodialup /noremote /nonetwork /nobatch"
$ write f "exit"
$ close f
$ goto run_authorize
$!
$! ---- a new account ----
$new_account:
$ uic = f$edit(p2, "UPCASE,TRIM")
$ if uic .eqs. "" .and. interactive then -
     inquire uic "UIC for ''account', a group of its own above ''maxsys' (e.g. [377,1])"
$ uic = f$edit(uic, "UPCASE,TRIM")
$ if uic .eqs. "" .or. f$extract(0, 1, uic) .nes. "[" .or. f$locate(",", uic) .eq. f$length(uic)
$ then
$   say "HASS$CONFIGURE: ''account' is a new account: give its UIC, e.g. [377,1]"
$   exit 44
$ endif
$ grp = f$integer("%O" + (f$element(0, ",", uic) - "["))
$ if grp .le. maxsys
$ then
$   say "HASS$CONFIGURE: group ", f$fao("!OL", grp), " is a system group (MAXSYSGROUP is ", maxsys, -
        "): choose a group above it (D9)"
$   exit 44
$ endif
$ ! a password nobody needs: 32 random characters from OpenSSL's generator
$ agent := $HASS$ROOT:[BIN]HASS_AGENT.EXE
$ define/user sys$output 'work'
$ agent --random
$ open/read f 'work'
$ read f password
$ close f
$ delete/erase/nolog 'work';*
$ if f$length(password) .ne. 32
$ then
$   say "HASS$CONFIGURE: couldn't make a password (HASS_AGENT.EXE --random)"
$   exit 44
$ endif
$ datadev = f$parse("HASS$ROOT:[DATA]",,,"DEVICE","NO_CONCEAL")
$ datadir = f$parse("HASS$ROOT:[DATA]",,,"DIRECTORY","NO_CONCEAL") - "][" - "><"
$ say "Creating account ''account' ''uic'"
$ open/write f 'work'
$ write f "add ''account' /uic=''uic' /password=''password' /owner=""vms-hass agent"" -"
$ write f "  /privileges=(netmbx,tmpmbx) /defprivileges=(netmbx,tmpmbx) -"
$ write f "  /flags=(nodisuser,disctly,lockpwd) /lgicmd=""NL:"" /pwdlifetime=none -"
$ write f "  /nolocal /nodialup /noremote /nonetwork /nobatch -"
$ write f "  /device=''datadev' /directory=''datadir'"
$ write f "exit"
$ close f
$ password = ""
$run_authorize:
$ set file/protection=(s:rwd,o,g,w) 'work'
$ define/user sysuaf 'uaf'
$ define/user sys$input 'work'
$ mcr authorize
$ delete/erase/nolog 'work';*
$ uicnum = f$identifier(account, "NAME_TO_NUMBER")
$ if uicnum .eq. 0
$ then
$   say "HASS$CONFIGURE: AUTHORIZE didn't create ''account' (see above)"
$   exit 44
$ endif
$!
$! ---- the tree ----
$ rootdev = f$parse("HASS$ROOT:[000000]",,,"DEVICE","NO_CONCEAL")
$ rootdir = f$parse("HASS$ROOT:[000000]",,,"DIRECTORY","NO_CONCEAL") - "][" - "><" - ".000000"
$ ! the root's own directory file, e.g. SYS$COMMON:[VMS$COMMON]HASS.DIR:
$ ! split "[A.B.C]" at its last dot into "[A.B]" and "C"
$ inner = rootdir - "[" - "]"
$ n = 0
$count_dots:
$ if f$element(n + 1, ".", inner) .eqs. "." then goto counted
$ n = n + 1
$ goto count_dots
$counted:
$ last = f$element(n, ".", inner)
$ parent = rootdev + "[000000]"
$ if n .gt. 0 then parent = rootdev + "[" + f$extract(0, f$length(inner) - f$length(last) - 1, inner) + "]"
$ set file/protection=(s:rwe,o:rwe,g:re,w:re) 'parent''last'.DIR
$ set file/protection=(s:rwe,o:rwe,g:re,w:re) HASS$ROOT:[000000]BIN.DIR,COM.DIR,DOC.DIR
$ set file/protection=(s:rwed,o:rwed,g:re,w:re) HASS$ROOT:[BIN]*.*;*,[COM]*.*;*,[DOC]*.*;*
$ set file/owner='account'/protection=(s:rwe,o:rwe,g,w) HASS$ROOT:[000000]CONF.DIR,DATA.DIR,LOG.DIR
$ if f$search("HASS$ROOT:[CONF]AGENT.CONF") .eqs. ""
$ then
$   copy/nolog HASS$ROOT:[CONF]AGENT.CONF_TEMPLATE HASS$ROOT:[CONF]AGENT.CONF
$   say "Created HASS$ROOT:[CONF]AGENT.CONF from its template"
$ endif
$ if f$search("HASS$ROOT:[CONF]*.*;*") .nes. "" then set file/owner='account' HASS$ROOT:[CONF]*.*;*
$ if f$search("HASS$ROOT:[DATA]*.*;*") .nes. "" then set file/owner='account' HASS$ROOT:[DATA]*.*;*
$ if f$search("HASS$ROOT:[LOG]*.*;*") .nes. "" then set file/owner='account' HASS$ROOT:[LOG]*.*;*
$ ! AGENT.CONF and the broker's password file: their owner only, every version (D7)
$ set file/protection=(s:rwed,o:rwed,g,w) HASS$ROOT:[CONF]AGENT.CONF;*
$ if f$search("HASS$ROOT:[CONF]MQTT.PWD") .nes. "" then -
     set file/protection=(s:rwed,o:rwed,g,w) HASS$ROOT:[CONF]MQTT.PWD;*
$ set file/protection=(s:rwed,o:rwed,g:r,w:r) HASS$ROOT:[CONF]AGENT.CONF_TEMPLATE;*
$!
$! ---- site settings ----
$ autostart = f$edit(p3, "UPCASE,TRIM")
$ if autostart .eqs. "" .and. interactive then inquire autostart "Start the agent at boot (YES/NO) [''autodef']"
$ autostart = f$edit(autostart, "UPCASE,TRIM")
$ if autostart .eqs. "" then autostart = autodef
$ if autostart .nes. "YES" then autostart = "NO"
$ open/write f SYS$MANAGER:HASS$CONFIG.COM
$ write f "$! HASS$CONFIG.COM - site settings for the vms-hass agent, written by"
$ write f "$! HASS$ROOT:[COM]HASS$CONFIGURE.COM (run it again to change them)"
$ write f "$ hass_account == ""''account'"""
$ write f "$ hass_autostart == ""''autostart'"""
$ close f
$ set file/protection=(s:rwed,o:rwed,g:re,w) SYS$MANAGER:HASS$CONFIG.COM
$ define/system/executive_mode HASS$USER 'account'
$!
$ say ""
$ say "The vms-hass agent will run as ''account' ", f$fao("!%U", uicnum), -
      " (start at boot: ''autostart')."
$ say "Next:"
$ say "  1. Put the broker's address and user in HASS$ROOT:[CONF]AGENT.CONF, and the"
$ say "     user's password, on one line, in HASS$ROOT:[CONF]MQTT.PWD; then run this"
$ say "     procedure again, so both belong to ''account' and only it can read them"
$ say "  2. $ @HASS$ROOT:[COM]HASS$CONTROL START"
$ say "  3. Add $ @SYS$STARTUP:HASS$STARTUP.COM to SYS$MANAGER:SYSTARTUP_VMS.COM (after"
$ say "     TCP/IP) and $ @SYS$STARTUP:HASS$SHUTDOWN.COM to SYS$MANAGER:SYSHUTDWN.COM"
$ exit 1
