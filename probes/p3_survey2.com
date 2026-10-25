$! p3_survey2.com - queues, shadowing licence, LD driver, privileges (read-only)
$ set noon
$ show queue/all/batch
$ show license volshad
$ show license vms-ld
$ write sys$output "SHDRIVER: ", f$search("sys$loadable_images:shdriver.exe")
$ write sys$output "LDDRIVER: ", f$search("sys$loadable_images:sys$lddriver.exe")
$ show device ld
$ show process/privileges
$ write sys$output "SHADOWING: ", f$getsyi("SHADOWING")
$ exit
