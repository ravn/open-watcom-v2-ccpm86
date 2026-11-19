#pmake: build small 16bit cpm86 _cpm ix86 static os_dos cpu_i86
processor=086
model=ms
system=cpm86
linkage=static

!include ../../../../mklib.mif

# The CP/M-86 C startup (_cstart_). Its entry sits in the front-sorted BEGTEXT
# segment (see a/cstartcpm.asm), so under `option dosseg` it lands at code-group
# offset 0 (the CP/M-86 fixed CS:0000 entry) even when pulled from a library.
# It is therefore ARCHIVED into clibs.lib (via _cpm/objects.mif) and auto-
# selected by the compiled object's default-library record, exactly like Open
# Watcom's 16-bit `system dos` startup -- so `system cpm86` needs NO `libfile`.
# The standalone object is ALSO emitted here for the explicit-`file` demo
# scripts (contrib/ravn/watcom-cpm86-libc/build.sh) that link it directly.
all : cstartcpm.obj .SYMBOLIC
