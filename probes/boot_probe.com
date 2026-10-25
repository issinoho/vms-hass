$! boot_probe.com - build and run boot_probe.c (Phase 2, DST and boot time)
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ cc/standard=c99/pointer_size=32/prefix_library_entries=all_entries boot_probe.c
$ if $severity .ne. 1 then write sys$output "BUILD-FAIL cc"
$ link boot_probe
$ if $severity .ne. 1 then write sys$output "BUILD-FAIL link"
$ run boot_probe
$ show system/noprocess
$ delete boot_probe.obj;*,boot_probe.exe;*
$ exit
