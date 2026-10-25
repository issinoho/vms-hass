$! agent.com - run the hand-built agent from the work directory (Phase 2).
$!   @agent PRINT          print discovery and state from two samples
$!   @agent RUN [conf]     run in this process until stopped (default AGENT.CONF)
$!   @agent REMOVE [conf]  remove this node's device from HA
$!   @agent COMPARE        the same figures from DCL, to check against PRINT
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ exe = f$parse("[.BUILD]HASS_AGENT.EXE")
$ agent := $'exe'
$ conf = p2
$ if conf .eqs. "" then conf = "AGENT.CONF"
$! PRINT also runs with only NETMBX and TMPMBX, as the agent will
$ if p1 .eqs. "PRINT"
$ then
$   old = f$setprv("NOALL,NETMBX,TMPMBX")
$   agent "--print" nl:
$   old = f$setprv(old)
$ endif
$! RUN drops to NETMBX and TMPMBX first: the agent must need no privilege
$ if p1 .eqs. "RUN"
$ then
$   old = f$setprv("NOALL,NETMBX,TMPMBX")
$   write sys$output "privileges: ", f$getjpi("","CURPRIV")
$   agent 'conf'
$ endif
$ if p1 .eqs. "REMOVE" then agent "--remove" 'conf'
$ if p1 .nes. "COMPARE" then exit
$ show memory/physical
$ show memory/files
$ show users
$ show device d/mounted
$ write sys$output "processes ", f$getsyi("PROCESS_COUNT") ! may not exist
$ monitor modes/nodisplay/summary/record=nl:/interval=2/ending="+0:00:06"
$ monitor states/nodisplay/summary/record=nl:/interval=2/ending="+0:00:04"
$ exit
