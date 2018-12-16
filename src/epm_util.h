/*
 * Copyright (c) 2003-2018 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MER-
 * CHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPE-
 * CIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTH-
 * ERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _EPM_UTIL_H_
#define _EPM_UTIL_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _WIN32
#pragma comment(lib,"Ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#ifdef UNIX
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#endif

typedef unsigned long long  uint64;
typedef long long           sint64;
typedef long long           int64;
typedef unsigned long       ulong;
typedef unsigned int        uint32;
typedef int                 sint32;
typedef int                 int32;
typedef unsigned short int  uint16;
typedef short int           sint16;
typedef short int           int16;
typedef unsigned char       uint8;
typedef char                sint8;
typedef char                int8;


#ifdef UNIX

#ifndef SLEEP 
#define SLEEP(x) usleep((x)*1000)
#endif

#ifndef SOCKET
#define SOCKET int
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET   -1
#endif

#ifndef WSAGetLastError
#define WSAGetLastError() errno
#endif

#ifndef closesocket
#define closesocket close
#endif

#ifndef OutputDebugString
#define OutputDebugString printf
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif


#endif /* end if UNIX */


#ifdef _WIN32
#ifndef SLEEP 
#define SLEEP Sleep
#endif

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif

#ifndef srandom
#define srandom srand
#endif

#ifndef random
#define random rand
#endif

#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

#ifndef snprintf
#define snprintf _snprintf
#endif

#define MSG_NOSIGNAL 0

#if _MSC_VER < 1300
#define strtoll(p,e,b) ((*(e)=(char*)(p)+(((b)== 0)?strspn((p),"0123456789"):0)),_atoi64(p))
#else
#define strtoll(p, e, b) _strtoi64(p, e, b) 
#endif

#ifndef strtoull
#define strtoull strtoul
#endif

#ifndef strcpy
#define strcpy winstrcpy
#endif

int gettimeofday(struct timeval *tv, struct timezone *tz);

#pragma warning(disable : 4996)
#pragma warning(disable : 4244)
#pragma warning(disable : 4133)
#pragma warning(disable : 4267)

#endif /* endif _WIN32 */


#define tv_diff_us(t1,t2) (((t2)->tv_sec-(t1)->tv_sec)*1000000+(t2)->tv_usec-(t1)->tv_usec)

#ifdef __cplusplus
extern "C" {
#endif


void epm_mem_print();
 
void * epm_alloc_dbg   (long size, char * file, int line);
void * epm_zalloc_dbg  (long size, char * file, int line);
void * epm_realloc_dbg (void * ptr, long size, char * file, int line);
void   epm_free_dbg    (void * ptr, char * file, int line);
 
#define epm_alloc(size)        epm_alloc_dbg((size), __FILE__, __LINE__)
#define epm_zalloc(size)       epm_zalloc_dbg((size), __FILE__, __LINE__)
#define epm_realloc(ptr, size) epm_realloc_dbg((ptr), (size), __FILE__, __LINE__)
#define epm_free(ptr)          epm_free_dbg((ptr), __FILE__, __LINE__)
 


/* Binary Time structure */
typedef struct BTime_ {
     long   s;
     long  ms;
} epm_time_t;
 
/* Return true if the tvp is related to uvp according to the relational
   operator cmp.  Recognized values for cmp are ==, <=, <, >=, and >. */
#define epm_time_cmp(tvp, cmp, uvp)              \
        (((tvp)->s == (uvp)->s) ?             \
         ((tvp)->ms cmp (uvp)->ms) :          \
         ((tvp)->s cmp (uvp)->s))
 
/* calling the system function 'ftime' to retrieve the current time,
 * precise can be milli-seconds */
long epm_time (epm_time_t * tp);
 
/* add the specified binary time into the time pointer.*/
void epm_time_add (epm_time_t * tp, epm_time_t tv);
 
/* add the specified milli-seconds into the epm_time_t pointer.*/
void epm_time_add_ms (epm_time_t * tp, long length);
 
/* get the current time and add the specified milli-seconds.*/
void epm_time_now_add (epm_time_t * tp, long ms);
 
/* return the epm_time_t result of 'time1 - time0' */
epm_time_t epm_time_diff (epm_time_t * time0, epm_time_t * time1);
 
/* return the milli-seconds result of 'time1 - time0' */
long epm_time_diff_ms (epm_time_t * time0, epm_time_t * time1);



#ifdef UNIX
 
/* CRITICAL_SECTION */
#define CRITICAL_SECTION   pthread_mutex_t
#define INIT_STATIC_CS(x)  pthread_mutex_t (x) = PTHREAD_MUTEX_INITIALIZER
int InitializeCriticalSection (CRITICAL_SECTION * cs);
int EnterCriticalSection      (CRITICAL_SECTION * cs);
int LeaveCriticalSection      (CRITICAL_SECTION * cs);
int DeleteCriticalSection     (CRITICAL_SECTION * cs);

#endif  //end if UNIX
 
/* create an instance of EVENT and initialize it. */
void * event_create ();
 
/* wait the event for specified time until being signaled .
   the time is milli-second */
int event_wait (void * event, int millisec);
 
/* set the event to signified to wake up the suspended thread. */
void event_set (void * event, int val);
 
/* destroy the instance of the event. */
void event_destroy (void * event);


#ifdef __cplusplus
}
#endif

#endif

