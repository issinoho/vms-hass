$! vax_hdr.com - which headers and item codes the agent uses exist on this VAX.
$!   vax.py run vax_hdr.com <work-dir>
$ set noon
$ set default 'p1'
$ dir/size sys$library:*.tlb
$ library/extract=(SYIDEF,DVIDEF,RMIDEF,QUIDEF,JPIDEF,PRVDEF,ISSDEF,EFNDEF,STARLET)/output=vax_hdr.tmp sys$library:sys$starlet_c.tlb
$ write sys$output "--- items"
$ search vax_hdr.tmp "SYI$_ARCH_NAME","SYI$_HW_NAME","SYI$_SCSSYSTEMID","SYI$_MEMSIZE","SYI$_PAGE_SIZE","SYI$_CLUSTER_MEMBER","SYI$_SWAPFILE_FREE","SYI$_MAXSYSGROUP"
$ search vax_hdr.tmp "DVI$_SHDW_","DVI$_ERRCNT","DVI$_STS ","DVI$_MAXBLOCK"
$ search vax_hdr.tmp "RMI$_MODES","RMI$_FRLIST","RMI$_MODLIST","RMI$_PROCS","RMI$_COM ","RMI$_COMO"
$ search vax_hdr.tmp "QUI$M_QUEUE_AUTOSTART_INACTIVE","QUI$_SCSNODE_NAME","QUI$M_SEARCH_WILDCARD"
$ search vax_hdr.tmp "JPI$_AUTHPRIV","ISS$C_ID_NATURAL","EFN$C_ENF","sys$persona_create","sys$persona_assume","sys$device_scan"
$ write sys$output "--- lib_c"
$ if f$search("sys$library:sys$lib_c.tlb") .nes. ""
$ then
$   library/extract=(NMADEF)/output=vax_nma.tmp sys$library:sys$lib_c.tlb
$   search vax_nma.tmp "NMA$C_CTLIN_BSN","NMA$C_CTLIN_BRC","NMA$M_CNT_TYP"
$   delete vax_nma.tmp;*
$ endif
$ write sys$output "--- crtl"
$ library/list sys$library:decc$rtldef.tlb /only=(STDIO,UNISTD,NETDB,POLL,STDLIB)
$ library/extract=(STDIO,UNISTD,NETDB)/output=vax_rtl.tmp sys$library:decc$rtldef.tlb
$ search vax_rtl.tmp "popen","usleep","getnameinfo","inet_pton","AF_INET6","gai_strerror","freeaddrinfo","socklen_t"
$ delete vax_hdr.tmp;*,vax_rtl.tmp;*
