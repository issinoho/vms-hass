$! hdr_extract.com - copy the DVIDEF, QUIDEF, UCBDEF-free item headers here (read-only)
$ set noon
$ library/extract=(dvidef,quidef,syidef,dcdef)/output=hdr_p3.h sys$library:sys$starlet_c.tlb
$ write sys$output "EXTRACT: ", $status
$ exit
