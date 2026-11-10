$! build_vms.com - build vms-hass with VSI C (DECISIONS.md D2).
$!
$!   @build_vms [TEST]
$!
$! Builds [.BUILD]HASS_AGENT.EXE (the core, the OpenSSL network code, the VMS
$! platform layer), [.BUILD]HASS_LAUNCH.EXE (starts the agent as its account,
$! D9) and [.BUILD]TEST_CORE.EXE; TEST runs the unit tests.
$! Qualifiers as vms-beszel's: C99, 32-bit pointers, PREFIX=ALL_ENTRIES so the
$! socket calls link (NOTES.md 2), BSD 4.4 sockaddr.
$! On VAX (D11, NOTES.md 15): Compaq C's own language mode (it refuses C99),
$! D_float, no OpenSSL (plain MQTT only) and no SYS$LIB_C.TLB.
$ set noon
$ say = "write sys$output"
$ define/nolog sys$error sys$output
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ if f$search("[]BUILD.DIR") .eqs. "" then create/directory [.build]
$ vax = f$getsyi("ARCH_NAME") .eqs. "VAX"
$ if vax then goto qualifiers_vax
$ if f$trnlnm("SSL3$INCLUDE") .eqs. ""
$ then
$   say "BUILD-FAIL: SSL3$INCLUDE is not defined (VSI OpenSSL 3 kit)"
$   exit 44
$ endif
$ define/process/nolog OPENSSL SSL3$INCLUDE:
$ open/write o [.build]ssl.opt
$ write o "SYS$LIBRARY:SSL3$LIBSSL_SHR32/SHARE"
$ write o "SYS$LIBRARY:SSL3$LIBCRYPTO_SHR32/SHARE"
$ close o
$ ccq = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES" + -
        "/FLOAT=IEEE_FLOAT/IEEE_MODE=DENORM_RESULTS" + -
        "/DEFINE=(_SOCKADDR_LEN)/NOLIST" + -
        "/INCLUDE_DIRECTORY=([.src.core],[.src.plat])"
$ sslopt = ",[.build]ssl.opt/options"
$ goto qualifiers_done
$qualifiers_vax:
$! no _SOCKADDR_LEN: it names getaddrinfo __BSD44_GETADDRINFO, which the VAX C
$! library doesn't have (NOTES.md 16); the code never reads sa_len
$ ccq = "/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/NOLIST" + -
        "/INCLUDE_DIRECTORY=([.src.core],[.src.plat])"
$ sslopt = ""
$qualifiers_done:
$ failed = 0
$ sources = "[.src.core]ha_buf,[.src.core]ha_fmt,[.src.core]ha_text,[.src.core]ha_json,[.src.core]ha_mqtt," + -
            "[.src.core]ha_conf,[.src.core]ha_model,[.src.core]ha_sess,[.src.core]ha_agent," + -
            "[.src.plat]ha_net_ossl,[.src.plat.vms]ha_plat_vms,[.src.main]ha_main," + -
            "[.src.plat.vms]ha_launch,[.tests.unit]test_core"
$ i = 0
$cc_loop:
$ s = f$element(i, ",", sources)
$ if s .eqs. "," then goto cc_done
$ i = i + 1
$ name = f$parse(s,,,"NAME","SYNTAX_ONLY")
$! The VMS platform reads the LAN counter layout (NMADEF) from SYS$LIB_C.TLB.
$ lib = ""
$ if f$edit(name, "UPCASE") .eqs. "HA_PLAT_VMS" .and. .not. vax then lib = "+SYS$LIBRARY:SYS$LIB_C/LIBRARY"
$ cc'ccq' 's'.c'lib' /object=[.build]'name'.obj
$ sev = $severity
$ if sev .ne. 1 .and. sev .ne. 3
$ then
$   say "BUILD-FAIL compile ", s, " (severity ", sev, ")"
$   failed = failed + 1
$ endif
$ goto cc_loop
$cc_done:
$ if failed .gt. 0
$ then
$   say "BUILD-FAIL: ", failed, " compile(s) failed"
$   exit 44
$ endif
$ core = "[.build]ha_buf,[.build]ha_fmt,[.build]ha_text,[.build]ha_json,[.build]ha_mqtt,[.build]ha_conf," + -
         "[.build]ha_model,[.build]ha_sess,[.build]ha_agent"
$! Undefined symbols are only warnings to LINK: check the severity.
$ link/executable=[.build]test_core.exe 'core',[.build]test_core
$ if $severity .ne. 1
$ then
$   say "BUILD-FAIL: link test_core"
$   exit 44
$ endif
$ link/executable=[.build]hass_agent.exe 'core',[.build]ha_net_ossl,[.build]ha_plat_vms,[.build]ha_main'sslopt'
$ if $severity .ne. 1
$ then
$   say "BUILD-FAIL: link hass_agent"
$   exit 44
$ endif
$ link/executable=[.build]hass_launch.exe [.build]ha_launch
$ if $severity .ne. 1
$ then
$   say "BUILD-FAIL: link hass_launch"
$   exit 44
$ endif
$ say "BUILD-OK"
$ if f$edit(p1, "UPCASE") .eqs. "TEST"
$ then
$   run [.build]test_core
$ endif
$ exit
