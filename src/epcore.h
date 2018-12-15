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

#ifndef _EPCORE_H_
#define _EPCORE_H_

#include "epm_util.h"
#include "epm_arr.h"
#include "epm_hashtab.h"
#include "epm_pool.h"

#ifdef __cplusplus      
extern "C" {           
#endif

typedef int IOHandler (void * vmgmt, void * pobj, int event, int fdtype);
typedef int GeneralCB (void * vpara, int status);
typedef int fd_set_poll (void * vepump, void * vpdev);
typedef int fd_dispatch (void * vepump, epm_time_t * delay);


typedef struct EPCore_ {

    int                maxfd;

    /* wake up the epoll_wait/select while waiting in block for the fd-set ready */
#ifdef HAVE_EVENTFD
    int                wakeupfd;
#else
    int                informport;
    SOCKET             informfd;
    SOCKET             wakeupfd;
#endif
    void             * wakeupdev;

    CRITICAL_SECTION   devicetableCS;
    epm_hashtab_t    * device_table;

    CRITICAL_SECTION   timertableCS;
    epm_hashtab_t    * timer_table;
    ulong              timerID;

    CRITICAL_SECTION   glbiodevlistCS;
    epm_arr_t        * glbiodev_list;
    CRITICAL_SECTION   glbiotimerlistCS;
    epm_arr_t        * glbiotimer_list;

    /* epump objects that represent the main/worker threads are organized in following list */
    CRITICAL_SECTION   threadlistCS;
    epm_arr_t        * thread_list;
    epm_hashtab_t    * thread_tab;
    int                threadindex;
    time_t             sorttime;

    /* default handler of all event generated during runtime */
    IOHandler        * callback;
    void             * cbpara;
 
    /* memory pool management */
    epm_pool_t       * device_pool;
    epm_pool_t       * timer_pool;
    epm_pool_t       * event_pool;

    /* configuration file API visiting handle */
    void             * hconf;

    uint8              quit;
    time_t             startup_time;

} epcore_t, *epcore_p;


void * epcore_new (int maxfd);
void   epcore_clean (void * vpcore);
void   epcore_start (void * vpcore, int maxnum);
void   epcore_stop (void * vpcore);

int epcore_set_callback (void * vpcore, void * cb, void * cbpara);

int    epcore_iodev_add (void * vpcore, void * vpdev);
void * epcore_iodev_del (void * vpcore, ulong id);
void * epcore_iodev_find (void * vpcore, ulong id);
int    epcore_iodev_tcpnum (void * vpcore);

int    epcore_iotimer_add (void * vpcore, void * vpdev);
void * epcore_iotimer_del (void * vpcore, ulong id);
void * epcore_iotimer_find (void * vpcore, ulong id);

int    epcore_thread_add (void * vpcore, void * vepump);
int    epcore_thread_del (void * vpcore, void * vepump);

void * epcore_thread_self (void * vpcore);
void * epcore_thread_select (void * vpcore);

int    epcore_thread_fdpoll (void * vpcore, void * vpdev);


int epcore_global_iodev_add (void * vpcore, void * vpdev);
int epcore_global_iodev_getmon (void * vpcore, void * veps);

int epcore_global_iotimer_add (void * vpcore, void * viot);
int epcore_global_iotimer_del (void * vpcore, void * viot);
int epcore_global_iotimer_getmon (void * vpcore, void * veps);


void epcore_print (void * vpcore);

#ifdef __cplusplus
}   
#endif


#endif

