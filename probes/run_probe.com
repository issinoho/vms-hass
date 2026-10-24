$! run_probe.com - Phase 0: build mqtt_probe.c and run it against the broker.
$!   @run_probe <broker-host> <port> <user> publish|delete
$! The password is read from MQTT.PWD in this directory (owner access only).
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ write sys$output "== platform"
$ write sys$output f$getsyi("nodename"), " ", f$getsyi("arch_name"), " ", f$getsyi("version")
$ tcpip show version
$ product show product ssl3*
$ write sys$output "SSL3 libssl 32: ", f$search("SYS$SHARE:SSL3$LIBSSL_SHR32.EXE")
$ cc/version
$ set file/protection=(s:rwed,o:rwed,g,w) mqtt.pwd
$ write sys$output "== build"
$! PREFIX=ALL_ENTRIES: in C99 mode only ISO C names map to DECC$, so the socket
$! calls stay undefined without it (as vms-beszel's build_vms.com found).
$ cc/standard=c99/pointer_size=32/prefix_library_entries=all_entries mqtt_probe.c
$ sev = $severity
$ if sev .ne. 1 then write sys$output "BUILD-FAIL cc severity ", sev
$ if sev .ne. 1 then exit
$ link mqtt_probe
$ sev = $severity
$ if sev .ne. 1 then write sys$output "BUILD-FAIL link severity ", sev
$ if sev .ne. 1 then exit
$ write sys$output "== run ''p4'"
$ probe := $'here'mqtt_probe.exe
$ probe "''p1'" "''p2'" "''p3'" mqtt.pwd "''p4'"
$ exit
