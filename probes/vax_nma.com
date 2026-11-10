$! vax_nma.com - where this VAX defines the NMA counter codes, and LANCP's counters.
$!   vax.py run vax_nma.com <work-dir>
$ set noon
$ set default 'p1'
$ dir/size sys$library:*.mlb
$ library/extract=$NMADEF/output=vax_nma.tmp sys$library:lib.mlb
$ search vax_nma.tmp "CTLIN_BSN","CTLIN_BRC","CNT_TYP","CNT_WID","CNT_MAP"
$ type/page=save=0 vax_nma.tmp /tail=0
$ delete vax_nma.tmp;*
$ mcr lancp show device xqa0/counters
