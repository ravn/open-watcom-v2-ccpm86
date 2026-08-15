/*
 * C Converted Whetstone Double Precision Benchmark
 *		Version 1.2	22 March 1998
 *
 *	(c) Copyright 1998 Painter Engineering, Inc.
 *		All Rights Reserved.
 *
 *		Permission is granted to use, duplicate, and
 *		publish this text and program as long as it
 *		includes this entire comment block and limited
 *		rights reference.
 *
 * Converted by Rich Painter, Painter Engineering, Inc. based on the
 * www.netlib.org benchmark/whetstoned version obtained 16 March 1998.
 *
 * (Local note: this copy is trimmed for a CP/M cycle-count oracle -- the
 * wall-clock timing, KIPS reporting, and command-line argument parsing were
 * removed, LOOP is a fixed compile-time constant, and POUT is always enabled
 * so the per-module check values are printed deterministically instead of a
 * self-timed MIPS figure.  The benchmark modules themselves are unchanged.)
 *
 * It is a floating-point stress test: modules exercise add/sub/mul/div on
 * double, array traffic, procedure calls, and the transcendental library
 * (sin, cos, atan, sqrt, exp, log).  On the Z80 every one of these is a
 * soft-float / libm call, so this primarily measures the double soft-float +
 * libm quality of each toolchain.
 */

#include <stdio.h>
#include <math.h>

/* External MAME rc759 timing: when built -DMAME_DONE, bracket the whole run
   with two OUT 0x2FE bus cycles (START=0xB000, END=0xE000). The rc759 driver
   does not decode 0x2FE, so the writes are side-effect-free on the guest, but a
   MAME io-space write-tap (mame-tests/whet_time.lua) reads machine.time at each
   and reports the elapsed EMULATED seconds -- i.e. the execution time measured
   from OUTSIDE, never self-timed inside Whetstone. Harmless under emu2. */
#ifdef MAME_DONE
#include "mamedone.h"
#define WHET_START() mame_done(0xB000)
#define WHET_END()   do { fflush(stdout); mame_done(0xE000); } while(0)
#else
#define WHET_START() ((void)0)
#define WHET_END()   ((void)0)
#endif

/* CP/M-86 Layer-2 seam init (see stdiotest.c / floattest.c): our minimal
   crt0 does not walk Watcom's init table, so we run the two DOS-free
   initializers ourselves -- __InitFiles() attaches the stdout FILE buffer,
   __setEFGfmt() points printf's %e/%f/%g formatter at Watcom's real
   _EFG_Format (mathlib efgfmt.c/ldcvt.c) instead of the noefgfmt stub. */
extern void __InitFiles( void );
extern void __setEFGfmt( void );

/* map the FORTRAN math functions to the C versions */
#define DSIN	sin
#define DCOS	cos
#define DATAN	atan
#define DLOG	log
#define DEXP	exp
#define DSQRT	sqrt

/* Fixed loop count (was time-driven / argv-driven in the original). */
#define LOOP 10

void POUT(long N, long J, long K, double X1, double X2, double X3, double X4);
void PA(double E[]);
void P0(void);
void P3(double X, double Y, double *Z);

/*	COMMON T,T1,T2,E1(4),J,K,L  */
double T, T1, T2, E1[5];
int J, K, L;

int main(void)
{
	long I;
	long N1, N2, N3, N4, N6, N7, N8, N9, N10, N11;
	double X1, X2, X3, X4, X, Y, Z;

	__InitFiles();
	__setEFGfmt();

	WHET_START();

	T  = .499975;
	T1 = 0.50025;
	T2 = 2.0;

	N1  = 0;
	N2  = 12 * LOOP;
	N3  = 14 * LOOP;
	N4  = 345 * LOOP;
	N6  = 210 * LOOP;
	N7  = 32 * LOOP;
	N8  = 899 * LOOP;
	N9  = 616 * LOOP;
	N10 = 0;
	N11 = 93 * LOOP;

	/* Module 1: Simple identifiers */
	X1 = 1.0; X2 = -1.0; X3 = -1.0; X4 = -1.0;
	for (I = 1; I <= N1; I++) {
		X1 = ( X1 + X2 + X3 - X4) * T;
		X2 = ( X1 + X2 - X3 + X4) * T;
		X3 = ( X1 - X2 + X3 + X4) * T;
		X4 = (-X1 + X2 + X3 + X4) * T;
	}
	POUT(N1, N1, N1, X1, X2, X3, X4);

	/* Module 2: Array elements */
	E1[1] = 1.0; E1[2] = -1.0; E1[3] = -1.0; E1[4] = -1.0;
	for (I = 1; I <= N2; I++) {
		E1[1] = ( E1[1] + E1[2] + E1[3] - E1[4]) * T;
		E1[2] = ( E1[1] + E1[2] - E1[3] + E1[4]) * T;
		E1[3] = ( E1[1] - E1[2] + E1[3] + E1[4]) * T;
		E1[4] = (-E1[1] + E1[2] + E1[3] + E1[4]) * T;
	}
	POUT(N2, N3, N2, E1[1], E1[2], E1[3], E1[4]);

	/* Module 3: Array as parameter */
	for (I = 1; I <= N3; I++)
		PA(E1);
	POUT(N3, N2, N2, E1[1], E1[2], E1[3], E1[4]);

	/* Module 4: Conditional jumps */
	J = 1;
	for (I = 1; I <= N4; I++) {
		if (J == 1) J = 2; else J = 3;
		if (J > 2)  J = 0; else J = 1;
		if (J < 1)  J = 1; else J = 0;
	}
	POUT(N4, J, J, X1, X2, X3, X4);

	/* Module 5: Omitted.  Module 6: Integer arithmetic */
	J = 1; K = 2; L = 3;
	for (I = 1; I <= N6; I++) {
		J = J * (K - J) * (L - K);
		K = L * K - (L - J) * K;
		L = (L - K) * (K + J);
		E1[L - 1] = J + K + L;
		E1[K - 1] = J * K * L;
	}
	POUT(N6, J, K, E1[1], E1[2], E1[3], E1[4]);

	/* Module 7: Trigonometric functions */
	X = 0.5; Y = 0.5;
	for (I = 1; I <= N7; I++) {
		X = T * DATAN(T2 * DSIN(X) * DCOS(X) /
			(DCOS(X + Y) + DCOS(X - Y) - 1.0));
		Y = T * DATAN(T2 * DSIN(Y) * DCOS(Y) /
			(DCOS(X + Y) + DCOS(X - Y) - 1.0));
	}
	POUT(N7, J, K, X, X, Y, Y);

	/* Module 8: Procedure calls */
	X = 1.0; Y = 1.0; Z = 1.0;
	for (I = 1; I <= N8; I++)
		P3(X, Y, &Z);
	POUT(N8, J, K, X, Y, Z, Z);

	/* Module 9: Array references */
	J = 1; K = 2; L = 3;
	E1[1] = 1.0; E1[2] = 2.0; E1[3] = 3.0;
	for (I = 1; I <= N9; I++)
		P0();
	POUT(N9, J, K, E1[1], E1[2], E1[3], E1[4]);

	/* Module 10: Integer arithmetic (N10 == 0, retained for fidelity) */
	J = 2; K = 3;
	for (I = 1; I <= N10; I++) {
		J = J + K;
		K = J + K;
		J = K - J;
		K = K - J - J;
	}
	POUT(N10, J, K, X1, X2, X3, X4);

	/* Module 11: Standard functions */
	X = 0.75;
	for (I = 1; I <= N11; I++)
		X = DSQRT(DEXP(DLOG(X) / T1));
	POUT(N11, J, K, X, X, X, X);

	WHET_END();

	return 0;
}

void PA(double E[])
{
	J = 0;
L10:
	E[1] = ( E[1] + E[2] + E[3] - E[4]) * T;
	E[2] = ( E[1] + E[2] - E[3] + E[4]) * T;
	E[3] = ( E[1] - E[2] + E[3] + E[4]) * T;
	E[4] = (-E[1] + E[2] + E[3] + E[4]) / T2;
	J += 1;
	if (J < 6)
		goto L10;
}

void P0(void)
{
	E1[J] = E1[K];
	E1[K] = E1[L];
	E1[L] = E1[J];
}

void P3(double X, double Y, double *Z)
{
	double X1, Y1;
	X1 = X;
	Y1 = Y;
	X1 = T * (X1 + Y1);
	Y1 = T * (X1 + Y1);
	*Z = (X1 + Y1) / T2;
}

void POUT(long N, long J, long K, double X1, double X2, double X3, double X4)
{
	printf("%7ld %7ld %7ld %12.4e %12.4e %12.4e %12.4e\n",
		N, J, K, X1, X2, X3, X4);
}
