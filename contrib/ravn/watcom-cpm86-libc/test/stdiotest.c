/* stdiotest.c -- proof that Open Watcom's GENUINE stdio FILE* write-path runs
   on CP/M-86, driven ONLY by our thin Layer-2 seams (stdioshim __qwrite +
   isatty, lowlevel arena heap for the FILE buffer). Every formatting, buffering
   and FILE-table line below is Watcom's UNCHANGED clib.

   Exercised entry points: printf, puts, fputs, fprintf (-> __fprtf -> fputc
   buffers into a malloc'd FILE buffer -> __flush -> our __qwrite -> BDOS).

   Oracle (hand-computed, independent of the compiler):
     printf 42 ok
     puts line
     fputs line
     fprintf 97406784        (123456L * 789L = 97406784)
   fflush(stdout) is explicit because our minimal crt0 does not walk Watcom's
   fini table (see reference_watcom_cpm86_startup_initfini.md); with isatty()
   reporting a tty the stream is line-buffered, but the final flush guarantees
   the tail is emitted regardless. */

#include <stdio.h>

/* Attach a __stream_link (buffer holder) to each std FILE. In a full Watcom
   startup this runs off the XI init table (via __InitRtns); our minimal CP/M-86
   crt0 defers table-walking, so we invoke this one genuinely-DOS-free initializer
   ourselves before the first stdio call. It only calls our arena malloc -- see
   reference_watcom_cpm86_startup_initfini.md for the documented crt0 upgrade. */
extern void __InitFiles( void );

int main( void )
{
    __InitFiles();
    printf( "printf %d %s\n", 42, "ok" );
    puts( "puts line" );
    fputs( "fputs line\n", stdout );
    fprintf( stdout, "fprintf %ld\n", 123456L * 789L );
    fflush( stdout );
    return( 0 );
}
