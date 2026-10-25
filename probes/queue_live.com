$! queue_live.com - the Phase 3 exit test in HA (D8): a batch queue HASS_TEST
$! is created, stopped with a job pending, started and deleted, holding each
$! state for 90 s so the running agent (every 30 s) publishes it and HA records
$! it.  Deleting the queue makes the agent remove its sensors from HA.  Needs
$! OPER; nothing else is touched.  Run in batch: it WAITs.
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ if f$getqui("DISPLAY_QUEUE","QUEUE_NAME","HASS_TEST") .nes. ""
$ then
$   write sys$output "QL-FAIL: a queue HASS_TEST already exists; not touching it"
$   exit 44
$ endif
$ node = f$getsyi("NODENAME")
$ open/write j ql_job.com
$ write j "$ exit"
$ close j
$ initialize/queue/batch/on='node'::/start/job_limit=1 hass_test
$ write sys$output "QL-STEP ", f$time(), " created (idle) ", $status
$ wait 00:01:30
$ stop/queue/next hass_test
$ submit/queue=hass_test/noprinter/log='here'ql_job.log ql_job.com
$ write sys$output "QL-STEP ", f$time(), " stopped, 1 pending ", $status
$ wait 00:01:30
$ start/queue hass_test
$ write sys$output "QL-STEP ", f$time(), " started ", $status
$ wait 00:01:30
$ stop/queue/next hass_test
$ wait 00:00:02
$ delete/queue hass_test
$ write sys$output "QL-STEP ", f$time(), " deleted ", $status
$ if f$search("ql_job.com") .nes. "" then delete ql_job.com;*
$ if f$search("ql_job.log") .nes. "" then delete ql_job.log;*
$ write sys$output "QL-DONE [", f$getqui("DISPLAY_QUEUE","QUEUE_NAME","HASS_TEST"), "]"
$ exit
