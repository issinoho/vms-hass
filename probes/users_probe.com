$! users_probe.com - can an unprivileged process count interactive users?
$ set noon
$ write sys$output "authorized: ", f$getjpi("","AUTHPRIV")
$ old = f$setprv("NOALL,NETMBX,TMPMBX")
$ write sys$output "now: ", f$getjpi("","CURPRIV")
$ show users
$ n = 0
$ ctx = ""
$ x = f$context("PROCESS", ctx, "MODE", "INTERACTIVE", "EQL")
$loop:
$ pid = f$pid(ctx)
$ if pid .eqs. "" then goto done
$ n = n + 1
$ goto loop
$done:
$ write sys$output "interactive processes visible without privilege: ", n
$ n = 0
$ ctx = ""
$loop2:
$ pid = f$pid(ctx)
$ if pid .eqs. "" then goto done2
$ n = n + 1
$ goto loop2
$done2:
$ write sys$output "processes visible without privilege: ", n
$ old = f$setprv(old)
$ show users
$ exit
