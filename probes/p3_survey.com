$! p3_survey.com - what Phase 3 has to work with on this node (read-only)
$ set noon
$ show device d/errors
$ show device d/mounted/full
$ show shadow
$ show cluster
$ show queue/all/batch
$ show queue/all/device
$ write sys$output "ERRLOG: ", f$trnlnm("SYS$ERRORLOG")
$ directory/size/date sys$errorlog:errlog.sys
$ exit
