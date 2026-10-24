$! cpu_compare.com - MONITOR MODES and the agent's --print over the same seconds
$ set noon
$ define sys$error sys$output
$ here = f$parse(f$environment("procedure"),,,"device") + f$parse(f$environment("procedure"),,,"directory")
$ set default 'here'
$ exe = f$parse("[.BUILD]HASS_AGENT.EXE")
$ agent := $'exe'
$ spawn/nowait/output=mon.lis monitor modes/nodisplay/summary=sys$output/interval=1/ending="+0:00:12"
$ wait 0:00:01
$ agent "--print" nl: /out=print.lis
$ agent "--print" nl:
$ agent "--print" nl:
$ agent "--print" nl:
$ wait 0:00:04
$ type mon.lis
$ show device ldm2585:/full
$ exit
