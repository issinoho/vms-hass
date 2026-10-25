$! queue_test.com - the queue problem sensor on a real queue (Phase 3, D8).
$! Creates a batch queue HASS_TEST on this node (needs OPER), stops it, submits
$! a job, starts it again, and deletes it.  After each step the agent prints its
$! state with only NETMBX and TMPMBX.  Nothing else is touched.  It WAITs, so
$! run it in batch (tools/vms.sh <node> batch probes/queue_test.com).
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ if f$getqui("DISPLAY_QUEUE","QUEUE_NAME","HASS_TEST") .nes. ""
$ then
$   write sys$output "QT-FAIL: a queue HASS_TEST already exists; not touching it"
$   exit 44
$ endif
$ exe = f$parse("[.BUILD]HASS_AGENT.EXE")
$ agent := $'exe'
$ node = f$getsyi("NODENAME")
$ open/write j qt_job.com
$ write j "$ exit"
$ close j
$ initialize/queue/batch/on='node'::/start/job_limit=1 hass_test
$ write sys$output "QT-STEP created ", $status
$ gosub look
$ stop/queue/next hass_test
$ submit/queue=hass_test/noprinter/log='here'qt_job.log qt_job.com
$ write sys$output "QT-STEP stopped+submitted ", $status
$ gosub look
$ start/queue hass_test
$ wait 00:00:03
$ write sys$output "QT-STEP started ", $status
$ gosub look
$ stop/queue hass_test
$ write sys$output "QT-STEP paused ", $status
$ gosub look
$ stop/queue/next hass_test
$ wait 00:00:02
$ delete/queue hass_test
$ write sys$output "QT-STEP deleted ", $status
$ gosub look
$ if f$search("qt_job.com") .nes. "" then delete qt_job.com;*
$ if f$search("qt_job.log") .nes. "" then delete qt_job.log;*
$ write sys$output "QT-DONE ", f$getqui("DISPLAY_QUEUE","QUEUE_NAME","HASS_TEST")
$ exit
$look:
$ show queue/full hass_test
$ old = f$setprv("NOALL,NETMBX,TMPMBX")
$ agent "--print" nl:
$ old = f$setprv(old)
$ return
