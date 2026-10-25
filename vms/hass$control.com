$! HASS$CONTROL.COM - start, stop or check the vms-hass agent.
$!
$!   @HASS$ROOT:[COM]HASS$CONTROL [START|STOP|RESTART|STATUS] [account]   (default START)
$!
$! START runs HASS$RUN.COM as a detached process named HASS_AGENT, with
$! its log in HASS$ROOT:[LOG]HASS_AGENT.LOG (five versions kept).  If the
$! account is given (P2), or else the system logical name HASS$USER names one,
$! the process runs as that user (which needs the privileges of a system
$! startup); otherwise as the caller.  It refuses an unknown account and any system-group UIC (D9).
$! HASS$USER is read from the system table only, never from
$! job or group tables other users could write (vms-beszel security review L3).
$! STOP asks the agent to stop (the file HASS$ROOT:[DATA]STOP.), waits up to
$! 20 s for it to close its connection, and only then stops the process.
$ set noon
$ say = "write sys$output"
$ action = f$edit(p1, "upcase,trim")
$ if action .eqs. "" then action = "START"
$ proc = f$environment("procedure")
$ dev = f$parse(proc,,,"device","no_conceal")
$ dir = f$parse(proc,,,"directory","no_conceal") - "][" - "><"
$ physroot = dev + (dir - ".COM]")     ! "DEV:[A.B" - add ".X]" for a subdirectory
$! Files are named from physroot, the tree this procedure is in, rather than
$! HASS$ROOT: a process logical name defined here would outlive the call and
$! hide the system one from the caller (vms-beszel NOTES.md 44).
$ user = f$edit(p2, "upcase,trim")
$ if user .eqs. "" then user = f$edit(f$trnlnm("HASS$USER", "LNM$SYSTEM_TABLE"), "upcase,trim")
$ if user .eqs. "" then user = f$edit(f$getjpi("", "username"), "upcase,trim")
$ stopfile = physroot + ".DATA]STOP."
$ name = "HASS_AGENT"
$ gosub find_pid
$ if action .eqs. "STATUS" then goto status
$ if action .eqs. "STOP" then goto stop
$ if action .eqs. "RESTART" then goto restart
$ if action .eqs. "START" then goto start
$ say "HASS$CONTROL: unknown action ''action' (START, STOP, RESTART or STATUS)"
$ exit 44
$!
$start:
$ if pid .nes. ""
$ then
$   say "HASS_AGENT is already running (PID ''pid')"
$   exit 1
$ endif
$ if f$search(physroot + ".BIN]HASS_AGENT.EXE") .eqs. ""
$ then
$   say "HASS$CONTROL: HASS$ROOT:[BIN]HASS_AGENT.EXE is missing"
$   exit 44
$ endif
$ if f$search(physroot + ".CONF]AGENT.CONF") .eqs. ""
$ then
$   say "HASS$CONTROL: there is no HASS$ROOT:[CONF]AGENT.CONF; copy AGENT.CONF_TEMPLATE and fill it in"
$   exit 44
$ endif
$! D9: never a system account (the agent checks its privileges itself)
$ uicnum = f$identifier(user, "name_to_number")
$ if uicnum .eq. 0
$ then
$   say "HASS$CONTROL: ''user' is not an account"
$   exit 44
$ endif
$ if uicnum / 65536 .le. f$getsyi("MAXSYSGROUP")
$ then
$   say "HASS$CONTROL: refusing to run the agent as ''user', which has a system UIC; define"
$   say "  the system logical name HASS$USER as an account of its own (see the README)"
$   exit 44
$ endif
$ if f$search(stopfile) .nes. "" then delete/nolog 'stopfile';*
$ if f$search(physroot + ".LOG]HASS_AGENT.LOG") .nes. "" then -
     purge/nolog/keep=5 'physroot'.LOG]HASS_AGENT.LOG
$ if user .eqs. f$edit(f$getjpi("", "username"), "upcase,trim")
$ then
$   run/detached sys$system:loginout.exe /authorize/process_name='name' -
        /input='physroot'.COM]HASS$RUN.COM -
        /output='physroot'.LOG]HASS_AGENT.LOG -
        /error='physroot'.LOG]HASS_AGENT.LOG
$ else
$!  As another account: RUN/DETACHED/UIC would keep our username and our
$!  authorized privileges (vms-beszel NOTES.md 36), so the launcher takes on the
$!  account's persona and creates the process under it (D9).
$   launch := $'physroot'.BIN]HASS_LAUNCH.EXE
$   launch 'user' 'physroot'.COM]HASS$RUN.COM 'physroot'.LOG]HASS_AGENT.LOG 'name'
$ endif
$ if .not. $status then exit $status
$ wait 0:0:2
$ gosub find_pid
$ if pid .eqs. ""
$ then
$   say "HASS$CONTROL: the agent did not start; see HASS$ROOT:[LOG]HASS_AGENT.LOG"
$   exit 44
$ endif
$ say "HASS_AGENT started (PID ''pid')"
$ exit 1
$!
$stop:
$ if pid .eqs. ""
$ then
$   say "HASS_AGENT is not running"
$   if action .eqs. "RESTART" then return
$   exit 1
$ endif
$ open/write f 'stopfile'
$ close f
$ n = 0
$stop_wait:
$ wait 0:0:1
$ gosub find_pid
$ if pid .eqs. "" then goto stopped
$ n = n + 1
$ if n .lt. 20 then goto stop_wait
$ say "HASS_AGENT did not stop in 20 s: stopping process ''pid'"
$ stop/id='pid'
$stopped:
$ say "HASS_AGENT stopped"
$ if action .eqs. "RESTART" then return
$ exit 1
$!
$restart:
$ if pid .nes. "" then gosub stop
$ goto start
$!
$status:
$ if pid .eqs. ""
$ then
$   say "HASS_AGENT is not running"
$ else
$   say "HASS_AGENT is running: PID ", pid, ", user ", f$edit(f$getjpi(pid,"username"),"trim"), -
        ", CPU ", f$getjpi(pid,"cputim")/100, " s, working set ", f$getjpi(pid,"wssize"), " pagelets"
$ endif
$ exit 1
$!
$! sets pid to the agent's process, or "": the name AND the account must
$! match, so another user's HASS_AGENT can't be mistaken for it (vms-beszel security review L4)
$find_pid:
$ pid = ""
$ ctx = ""
$fp_loop:
$ found = f$pid(ctx)
$ if found .eqs. "" then return
$ if f$getjpi(found,"prcnam") .nes. name then goto fp_loop
$ if f$edit(f$getjpi(found,"username"),"upcase,trim") .nes. user then goto fp_loop
$ pid = found
$ if f$type(ctx) .eqs. "PROCESS_CONTEXT" then tmp = f$context("PROCESS", ctx, "CANCEL")
$ return
