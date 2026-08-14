/* portme.h -- stdcbench 0.8 port config for the Open Watcom -> CP/M-86 retarget
   (Watcom-clib twin of owc-drc/stdcbench/portme.h).

   Selects the integer benchmark set (c90base + c90lib); the float/double
   modules are upstream stubs in 0.8 and pull no <math.h>, so they are excluded.
   stdcbench_clock() (scbport.c) returns milliseconds read from BDOS T_GET
   (fn 105); STDCBENCH_CLOCKS_PER_SEC is therefore 1000. */
typedef unsigned long stdcbench_clock_t;
#define STDCBENCH_CLOCKS_PER_SEC 1000ul

#define C90BASE
#undef  C90FLOAT
#undef  C90DOUBLE
#define C90LIB
