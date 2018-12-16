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

#ifndef _EPUMP_INTERNAL_H_
#define _EPUMP_INTERNAL_H_

#include "epm_util.h"
#include "epm_list.h"
#include "epm_hashtab.h"
#include "epm_arr.h"

#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#endif

#ifdef __cplusplus      
extern "C" {           
#endif

#define MAX_EPOLL_TIMEOUT_MSEC (35*60*1000)

typedef struct EPump_ {
    void             * res[2];

  #ifdef HAVE_EPOLL
    int                epoll_size;      /* maximum concurrent FD for monitoring, get from conf */
    int                epoll_fd;        /* epoll file descriptor */
    struct epoll_event * epoll_events;
  #endif 
  #if defined(HAVE_SELECT) || defined(_WIN32)
    CRITICAL_SECTION   fdsetCS;
    fd_set             readFds;         /* read file descriptor set */
    fd_set             writeFds;        /* write file descriptor set */
    fd_set             exceptionFds;    /* exception file descriptor set */
  #endif 

    fd_set_poll      * fdpoll;
    fd_set_poll      * fdpollclear;
    fd_dispatch      * fddispatch;

    /* manage all the device being probed in select_set 
     * the instance stored in the list is from DeviceDesc */
    CRITICAL_SECTION   devicetableCS;
  #ifdef HAVE_EPOLL
    epm_hashtab_t    * device_table;
  #else
    epm_arr_t        * device_list;
  #endif 

    /* all the timer instance arranged in a sorted list */
    CRITICAL_SECTION   timerlistCS;
    epm_arr_t        * timer_list;

    /* when select returned, some fd read/write event occur. probing system
     * will generate some reading or writing events to fill into the event list.
     * and set the event to signal state to wake up all the blocking thread. */
    CRITICAL_SECTION   ioeventlistCS;
    epm_list_t       * ioevent_list;
    void             * ioevent;

    /* external event register management */
    CRITICAL_SECTION   exteventlistCS;
    epm_arr_t        * exteventlist;
    int                exteventindex;

    /* current threads management */
    ulong              threadid;
#ifdef _WIN32
    HANDLE             epumphandle;
#endif

    uint8              quit;
    uint8              blocking;
    int                deblock_times;

    epcore_t         * epcore;
} epump_t, *epump_p;


void * epump_new (epcore_t * epcore);
void   epump_free (void * vepump);

int epump_cmp_threadid (void * a, void * b);
int epump_cmp_epump_by_objnum (void * a, void * b);
int epump_cmp_epump_by_devnum (void * a, void * b);
int epump_cmp_epump_by_timernum (void * a, void * b);

int epump_objnum (void * veps, int type);
ulong  epumpid (void * veps);

int    epump_iodev_add (void * veps, void * vpdev);
void * epump_iodev_del (void * veps, SOCKET fd);
void * epump_iodev_find (void * vepump, SOCKET fd);
int    epump_iodev_tcpnum(void * vepump);
SOCKET epump_iodev_maxfd (void * vepump);
int    epump_iodev_scan  (void * vepump);

int epump_main_start (void * vpcore, int forkone);

//int epump_worker_run (void * veps);

#ifdef __cplusplus
}   
#endif


#endif

