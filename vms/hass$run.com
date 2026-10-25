$! HASS$RUN.COM - the vms-hass agent's detached process (run by HASS$STARTUP.COM).
$!
$! Runs HASS$ROOT:[BIN]HASS_AGENT.EXE and starts it again if it exits,
$! waiting 15 s, then twice as long after each quick failure (up to 5 minutes);
$! a run of 10 minutes or more resets the wait.  It stops for good when
$! HASS$ROOT:[DATA]STOP. exists (HASS$CONTROL STOP creates it).
$!
$! HASS$ROOT is taken from this procedure's own location when it isn't
$! defined, so a tree anywhere works without a system logical name.
$ set noon
$ say = "write sys$output"
$ set process/privileges=(noall,netmbx,tmpmbx)  ! all the agent ever needs (D9)
$! HASS$ROOT from the system table only (not job or group tables: vms-beszel
$! security review L3), else from this procedure's location; pinned in the process table
$ root = f$trnlnm("HASS$ROOT", "LNM$SYSTEM_TABLE", , "EXECUTIVE")
$ if root .eqs. ""
$ then
$   proc = f$environment("procedure")
$   dev = f$parse(proc,,,"device","no_conceal")
$   dir = f$parse(proc,,,"directory","no_conceal") - "][" - "><"
$   root = dev + (dir - ".COM]") + ".]"
$ endif
$ define/process/nolog/translation=concealed HASS$ROOT 'root'
$ say "HASS$RUN starting at ", f$time(), " as ", f$getjpi("","username"), -
      " with ", f$getjpi("","curpriv")
$ agent := $HASS$ROOT:[BIN]HASS_AGENT.EXE
$ delay = 15
$loop:
$ if f$search("HASS$ROOT:[DATA]STOP.") .nes. "" then goto done
$ started = f$time()
$ say "--- agent starting at ", f$time()
$ agent
$ status = $status
$ say "--- agent exited at ", f$time(), " status ", status
$ if f$search("HASS$ROOT:[DATA]STOP.") .nes. "" then goto done
$ if status .eq. 44                    ! SS$_ABORT: the agent refused this account (D9)
$ then
$   say "--- the agent refuses to run as this account (see above): not restarting"
$   goto done
$ endif
$! a long run resets the wait; quick failures back off
$ if f$cvtime("''started'+0-00:10:00.00","COMPARISON") .les. f$cvtime("","COMPARISON") then delay = 15
$ say "--- restarting in ", delay, " s"
$ mins = delay / 60
$ secs = delay - mins * 60
$ wait 0:'mins':'secs'
$ delay = delay * 2
$ if delay .gt. 300 then delay = 300
$ goto loop
$done:
$ say "HASS$RUN stopped at ", f$time()
$ exit
