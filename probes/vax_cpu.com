$! vax_cpu.com - MONITOR MODES and the agent's --print over the same seconds, with a
$! DCL loop running (DCL runs in supervisor mode), to check the mode mapping on VAX.
$!   vax.sh run vax_cpu.com      (in the work directory, after build_vms)
$ set noon
$ define sys$error sys$output
$ agent := $'f$parse("[.BUILD]HASS_AGENT.EXE")'
$ open/write l loop.com
$ write l "$ i = 0"
$ write l "$a: i = i + 1"
$ write l "$ if i .lt. 400000 then goto a"
$ close l
$ spawn/nowait/output=nl:/process=HASS_CPULOAD @loop.com
$ spawn/nowait/output=mon.lis monitor modes/nodisplay/summary=sys$output/interval=2/ending="+0:00:14"
$ wait 0:00:01
$ agent "--print" nl:
$ agent "--print" nl:
$ agent "--print" nl:
$ wait 0:00:08
$ type mon.lis

$ stop HASS_CPULOAD
$ delete loop.com;*,mon.lis;*
