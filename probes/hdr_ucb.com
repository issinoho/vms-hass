$! hdr_ucb.com - UCBDEF's status bits (read-only)
$ set noon
$ library/extract=ucbdef/output=hdr_ucb.h sys$library:sys$lib_c.tlb
$ write sys$output "EXTRACT: ", $status
$ exit
