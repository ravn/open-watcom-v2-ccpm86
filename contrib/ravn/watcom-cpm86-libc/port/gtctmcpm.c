/* gtctmcpm.c -- CP/M-86 implementation of Open Watcom's __getctime() clock
 * seam (declared in getctime.h, called by the stock time()/ftime()/clock()).
 * This is the correct in-architecture port point: time.c stays unchanged and
 * calls __getctime(&tm) + mktime(); only this bottom primitive is OS-specific,
 * exactly as the DOS build supplies gtctm.c (int 21h) and OS/2 supplies
 * gtctmos2.c.
 *
 * WALL CLOCK AVAILABILITY -- READ THIS.  "Get date and time" is BDOS function
 * 105 / 0x69 (T_GET), which is a *Concurrent CP/M-86 / CP/M-86 Plus* call.
 * Plain single-tasking CP/M-86 1.x has NO system clock and does not implement
 * fn 105, and even on 3.x the underlying XIOS must actually maintain a TOD for
 * it to advance.  We therefore gate on the BDOS version (fn 12, S_BDOSVER):
 * only >= 0x30 (CP/M 3.0+ / the RC759's Concurrent CP/M-86 3.1) is trusted to
 * carry T_GET.  On anything older the clock is unavailable and we report the
 * Unix epoch (1970-01-01 00:00:00) as an honest "not set" value rather than
 * fabricating a plausible current time.
 *
 * T_GET fills, at DS:DX, a 4-byte structure { word day; byte hour; byte min }
 * where `day` is a 1-based count from 1978-01-01 (day 1 == 1978-01-01) and
 * hour/min are BCD; it returns the seconds (BCD) in AL.  We hand these to the
 * struct tm as tm_year=78, tm_mon=0, tm_mday=day, letting the stock mktime()
 * normalise the (large) day-of-January into the real calendar date -- so no
 * date arithmetic lives here.  No timezone is applied (_timezone==0), i.e. the
 * CP/M TOD is treated as UTC.
 */

#include "variety.h"
#include <time.h>
#include "getctime.h"

/* BDOS gateway: INT 0E0h, function in CL, near &param in DX, byte result in AL.
 * (DS already addresses the one small-model data group, so a near & of the
 * static TOD struct is the offset the BDOS wants; a stack local would sit in SS
 * != DS and the BDOS would scribble the wrong segment.) */
extern unsigned char _bdos_b( unsigned char fn, void *param );
#pragma aux _bdos_b =           \
    "int 0E0h"                  \
    parm [cl] [dx]              \
    value [al]                  \
    modify [ax bx cx dx es];

#define BD_VERSION  12          /* S_BDOSVER: >= 0x30 => CP/M 3+ / Concurrent */
#define BD_TIMEGET  105         /* T_GET: Concurrent CP/M-86 get date and time */

struct _cpm_tod {               /* layout BDOS T_GET fills */
    unsigned short day;         /* 1-based, day 1 == 1978-01-01 */
    unsigned char  hour;        /* BCD */
    unsigned char  min;         /* BCD */
};

static int _bcd2( unsigned char b )
{
    return( (b >> 4) * 10 + (b & 0x0f) );
}

int _WCNEAR __getctime( struct tm *ti )
{
    static struct _cpm_tod tod;         /* static: must live in DGROUP (DS) */
    unsigned char          secbcd;

    /* Concurrent CP/M-86 / CP/M-86 Plus only: fn 105 is absent on CP/M-86 1.x */
    if( (_bdos_b( BD_VERSION, 0 ) & 0xFF) < 0x30 ) {
        ti->tm_sec = ti->tm_min = ti->tm_hour = 0;
        ti->tm_mday = 1;                /* 1970-01-01: clock unavailable */
        ti->tm_mon  = 0;
        ti->tm_year = 70;
        ti->tm_isdst = 0;
        return( 0 );
    }

    tod.day = 0;
    tod.hour = 0;
    tod.min = 0;
    secbcd = _bdos_b( BD_TIMEGET, &tod );

    ti->tm_sec  = _bcd2( secbcd );
    ti->tm_min  = _bcd2( tod.min );
    ti->tm_hour = _bcd2( tod.hour );
    /* day 1 == 1978-01-01; feed it as "the Nth of January 1978" and let
     * mktime() carry the overflow into the correct month/year. */
    ti->tm_mday = (tod.day ? tod.day : 1);
    ti->tm_mon  = 0;
    ti->tm_year = 78;
    ti->tm_isdst = 0;
    return( 0 );                        /* milliseconds unknown -> 0 */
}
