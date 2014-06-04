/*
 * Author:  David Robert Nadeau
 * Site:    http://NadeauSoftware.com/
 * License: Creative Commons Attribution 3.0 Unported License
 *          http://creativecommons.org/licenses/by/3.0/deed.en_US
 */

#if defined(_WIN32)
#include <malloc.h>
#include <Windows.h>


#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>	/* POSIX flags */
#include <time.h>	/* clock_gettime(), time() */
#include <sys/time.h>	/* gethrtime(), gettimeofday() */
#include <stdlib.h>

#if defined(__MACH__) && defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#else
#error "Unable to define getRealTime( ) for an unknown OS."
#endif
#include "getRealTime.h"
/**
 * Returns the real time, in microseconds, or -1.0 if an error occurred.
 *
 * Time is measured since an arbitrary and OS-dependent start time.
 * The returned real time is only useful for computing an elapsed time
 * between two calls to this function.
 */
void getRealTime(double *value)
{
#if defined(_WIN32)
	FILETIME tm;
	ULONGLONG t;
#if defined(NTDDI_WIN8) && NTDDI_VERSION >= NTDDI_WIN8
	/* Windows 8, Windows Server 2012 and later. ---------------- */
	GetSystemTimePreciseAsFileTime( &tm );
#else
	/* Windows 2000 and later. ---------------------------------- */
	GetSystemTimeAsFileTime( &tm );
#endif
	t = ((ULONGLONG)tm.dwHighDateTime << 32) | (ULONGLONG)tm.dwLowDateTime;
	*value = (double)t*100;
	return ;

#elif defined(_POSIX_VERSION)
	/* POSIX. --------------------------------------------------- */
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
	{
        struct timespec ts;
        int ret = clock_gettime(CLOCK_REALTIME, &ts);
        if ( ret!= -1 ){
            *value = ((double)ts.tv_sec)*1.e9+((double)ts.tv_nsec);
            return;
        }

		/* Fall thru. */
	}
#endif /* _POSIX_TIMERS */
	/* AIX, BSD, Cygwin, HP-UX, Linux, OSX, POSIX, Solaris. ----- */
    printf("getRealTime failed\n");
#endif
}

int aligned_mem(void **memptr, size_t alignment, size_t size){
#if defined(_WIN32)
    void *tmp=NULL;
    tmp = _aligned_malloc(size,alignment);
    *memptr = tmp;
	static int a=0;
	printf("Add tmp 0x%x %d\n",(int )tmp,a++);
    return tmp==NULL?1:0;
#elif defined(_POSIX_VERSION)
    /* POSIX. --------------------------------------------------- */
    return posix_memalign(memptr,alignment, size);
#endif
    
}
