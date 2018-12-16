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

#include "epm_util.h"
#include "epm_arr.h"
#include "epm_hashtab.h"
#include "epm_pool.h"
#include "epm_sock.h"

#include "epcore.h"
#include "epump_internal.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epwakeup.h"

#ifdef UNIX
#include <sys/resource.h>

static int set_fd_limit(int max)
{
    struct rlimit rlim;
    struct rlimit rlim_new;

    if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        return -1;
    }

    if (rlim.rlim_cur >= max) return 0;

    if (rlim.rlim_max == RLIM_INFINITY || rlim.rlim_max >= max){
        rlim_new.rlim_max = rlim.rlim_max;
        rlim_new.rlim_cur = max;
    } else {
        rlim_new.rlim_max = rlim_new.rlim_cur = max;
    }

    if (setrlimit(RLIMIT_NOFILE, &rlim_new) != 0) {
      /* failed. try raising just to the old max */
      setrlimit(RLIMIT_NOFILE, &rlim);
      return -100;
    }

     return 0;
}
#else
static int set_fd_limit(int max)
{
	return 0;
}
#endif

void * epcore_new (int maxfd)
{
    epcore_t * pcore = NULL;
#ifdef _WIN32
    WSADATA wsd;
#endif

#ifdef _WIN32
    if (WSAStartup (MAKEWORD(2,2), &wsd) != 0) {
        return NULL;
    }
#endif

    pcore = epm_zalloc(sizeof(*pcore));
    if (!pcore) return NULL;

    if (maxfd <= 1024) maxfd = 65535;
    pcore->maxfd = maxfd;
    set_fd_limit(maxfd);

    time(&pcore->startup_time);
    pcore->quit = 0;

    pcore->wakeupfd = INVALID_SOCKET;
    pcore->wakeupdev = NULL;

    /* initialize memory pool resource */
    if (!pcore->device_pool) {
        pcore->device_pool = epm_pool_init(NULL);
        epm_pool_set_initfunc(pcore->device_pool, iodev_init);
        epm_pool_set_freefunc(pcore->device_pool, iodev_free);
        epm_pool_set_unitsize(pcore->device_pool, sizeof(iodev_t));
        epm_pool_set_allocnum(pcore->device_pool, 256);
    }

    if (!pcore->timer_pool) {
        pcore->timer_pool = epm_pool_init(NULL);
        epm_pool_set_freefunc(pcore->timer_pool, iotimer_free);
        epm_pool_set_unitsize(pcore->timer_pool, sizeof(iotimer_t));
        epm_pool_set_allocnum(pcore->timer_pool, 256);
    }

    if (!pcore->event_pool) {
        pcore->event_pool = epm_pool_init(NULL);
        epm_pool_set_freefunc(pcore->event_pool, ioevent_free);
        epm_pool_set_unitsize(pcore->event_pool, sizeof(ioevent_t));
        epm_pool_set_allocnum(pcore->event_pool, 64);
    }

    /* initialization of IODevice operation & management */
    InitializeCriticalSection(&pcore->devicetableCS);
    pcore->device_table = epm_ht_only_new(300000, iodev_cmp_id);
    epm_ht_set_hash_func(pcore->device_table, iodev_hash_func);

    /* initialization of IOTimer operation & management */
    InitializeCriticalSection(&pcore->timertableCS);
    pcore->timer_table = epm_ht_only_new(300000, iotimer_cmp_id);
    epm_ht_set_hash_func(pcore->timer_table, iotimer_hash_func);
    pcore->timerID = 100;

    InitializeCriticalSection(&pcore->glbiodevlistCS);
    pcore->glbiodev_list = epm_arr_new(32);
    InitializeCriticalSection(&pcore->glbiotimerlistCS);
    pcore->glbiotimer_list = epm_arr_new(64);

    InitializeCriticalSection(&pcore->threadlistCS);
    pcore->thread_list = epm_arr_new(32);
    pcore->thread_tab = epm_ht_only_new(300, epump_cmp_threadid);
    pcore->threadindex = 0;

    epcore_wakeup_init(pcore);

    return pcore;
}


void epcore_clean (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;

    if (!pcore) return;

    pcore->quit = 1;
    epcore_wakeup_send(pcore);
    SLEEP(10);
    
    /* clean the IODevice facilities */
    DeleteCriticalSection(&pcore->devicetableCS);
    epm_ht_free_all(pcore->device_table, iodev_free);
    pcore->device_table = NULL;

    /* clean the IOTimer facilities */
    DeleteCriticalSection(&pcore->timertableCS);
    epm_ht_free_all(pcore->timer_table, iotimer_free);
    pcore->timer_table = NULL;

    /* free the global iodev/iotimer list */
    DeleteCriticalSection(&pcore->glbiodevlistCS);
    epm_arr_free(pcore->glbiodev_list);
    pcore->glbiodev_list = NULL;
    DeleteCriticalSection(&pcore->glbiotimerlistCS);
    epm_arr_free(pcore->glbiotimer_list);
    pcore->glbiotimer_list = NULL;

    /* free the resource of thread management */
    DeleteCriticalSection(&pcore->threadlistCS);
    epm_arr_free(pcore->thread_list);
    pcore->thread_list = NULL;
    epm_ht_free(pcore->thread_tab);
    pcore->thread_tab = NULL;

    epcore_wakeup_clean(pcore);

    /* release all memory pool resource */
    epm_pool_clean(pcore->timer_pool);
    epm_pool_clean(pcore->device_pool);
    epm_pool_clean(pcore->event_pool);

    epm_free(pcore);

#ifdef _WIN32
    WSACleanup();
#endif
}

void epcore_start (void * vpcore, int maxnum)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    int         i = 0;

    if (!pcore) return;

    if (maxnum < 3) maxnum = 3;

    for (i = 0; i < maxnum - 1; i++) {
        epump_main_start(pcore, 1);
    }

    epump_main_start(pcore, 0);
}

void epcore_stop (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *)vpcore;

    pcore->quit = 1;
    epcore_wakeup_send(pcore);
}



int epcore_set_callback (void * vpcore, void * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;

    if (!pcore) return -1;

    pcore->callback = (IOHandler *)cb;
    pcore->cbpara = cbpara;

    return 0;
}
 
int epcore_iodev_add (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
 
    if (!pcore || !pdev) return -1;
 
    EnterCriticalSection(&pcore->devicetableCS);
    epm_ht_set(pcore->device_table, &pdev->id, pdev);
    LeaveCriticalSection(&pcore->devicetableCS);
 
    return 0;
}
 
void * epcore_iodev_del (void * vpcore, ulong id)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
 
    if (!pcore) return NULL;
 
    EnterCriticalSection(&pcore->devicetableCS);
    pdev = epm_ht_delete(pcore->device_table, &id);
    LeaveCriticalSection(&pcore->devicetableCS);
 
    return pdev;
}

void * epcore_iodev_find (void * vpcore, ulong id)
{
    epcore_t * pcore = (epcore_t *) vpcore;
    iodev_t  * pdev = NULL;
 
    if (!pcore) return NULL;
 
    EnterCriticalSection(&pcore->devicetableCS);
    pdev = epm_ht_get(pcore->device_table, &id);
    LeaveCriticalSection(&pcore->devicetableCS);
 
    return pdev;
}
 
int epcore_iodev_tcpnum (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    iodev_t   * pdev = NULL;
    int         i, num, retval = 0;
 
    if (!pcore) return 0;
 
    EnterCriticalSection(&pcore->devicetableCS);
    num = epm_ht_num(pcore->device_table);
    for (i=0; i<num; i++) {
        pdev = epm_ht_value(pcore->device_table, i);
        if (!pdev) continue;
        if (pdev->fdtype == FDT_CONNECTED || pdev->fdtype == FDT_ACCEPTED)
            retval++;
    }
    LeaveCriticalSection(&pcore->devicetableCS);
 
    return retval;
}


int epcore_iotimer_add (void * vpcore, void * viot)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    iotimer_t  * iot = (iotimer_t *)viot;

    if (!pcore || !iot) return -1;

    EnterCriticalSection(&pcore->timertableCS);
    epm_ht_set(pcore->timer_table, &iot->id, iot);
    LeaveCriticalSection(&pcore->timertableCS);

    return 0;
}

void * epcore_iotimer_del (void * vpcore, ulong id)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    iotimer_t  * iot = NULL;

    if (!pcore) return NULL;

    EnterCriticalSection(&pcore->timertableCS);
    iot = epm_ht_delete(pcore->timer_table, &id);
    LeaveCriticalSection(&pcore->timertableCS);

    return iot;
}

void * epcore_iotimer_find (void * vpcore, ulong id)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    iotimer_t * iot = NULL;

    if (!pcore) return NULL;

    EnterCriticalSection(&pcore->timertableCS);
    iot = epm_ht_get(pcore->timer_table, &id);
    LeaveCriticalSection(&pcore->timertableCS);

    return iot;
}


int epcore_thread_add (void * vpcore, void * vepump)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    epump_t   * epump = (epump_t *)vepump;

    if (!pcore) return -1;
    if (!epump) return -2;

    EnterCriticalSection(&pcore->threadlistCS);
    if (epm_ht_get(pcore->thread_tab, &epump->threadid) != epump) {
        epm_ht_set(pcore->thread_tab, &epump->threadid, epump);
        epm_arr_push(pcore->thread_list, epump);
    }
    LeaveCriticalSection(&pcore->threadlistCS);

    return 0;
}

int epcore_thread_del (void * vpcore, void * vepump)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    epump_t   * epump = (epump_t *)vepump;
         
    if (!pcore) return -1; 
    if (!epump) return -2;
         
    EnterCriticalSection(&pcore->threadlistCS);
    if (epm_ht_delete(pcore->thread_tab, &epump->threadid) == epump)
        epm_arr_delete_ptr(pcore->thread_list, epump);
    LeaveCriticalSection(&pcore->threadlistCS);
 
    return 0; 
}

int epcore_thread_sort (void * vpcore, int type)
{  
    epcore_t  * pcore = (epcore_t *) vpcore; 
           
    if (!pcore) return -1;  
          
    if (type == 1) 
        epm_arr_sort_by(pcore->thread_list, epump_cmp_epump_by_devnum);
    else if (type == 2)
        epm_arr_sort_by(pcore->thread_list, epump_cmp_epump_by_timernum);
    else
        epm_arr_sort_by(pcore->thread_list, epump_cmp_epump_by_objnum);
    time(&pcore->sorttime);
     
    return 0;  
}

void * epcore_thread_self (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore; 
    epump_t   * epump = NULL;
    ulong       threadid = 0;
           
    if (!pcore) return NULL;  

#ifdef UNIX
    threadid = pthread_self();
#endif
#ifdef _WIN32
    threadid = GetCurrentThreadId();
#endif

    EnterCriticalSection(&pcore->threadlistCS);
    epump = epm_ht_get(pcore->thread_tab, &threadid);
    LeaveCriticalSection(&pcore->threadlistCS);

    return epump;
}


void * epcore_thread_select (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore; 
    epump_t   * epump = NULL;
           
    if (!pcore) return NULL;  

    EnterCriticalSection(&pcore->threadlistCS);
    if (time(NULL) - pcore->sorttime > 5)
        epcore_thread_sort(pcore, 1);

    epump = epm_arr_value(pcore->thread_list, 0);
    LeaveCriticalSection(&pcore->threadlistCS);

    return epump;
}

int epcore_thread_fdpoll (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
    epump_t   * epump = NULL;
    int         i, num;
 
    if (!pcore) return -1;
    if (!pdev) return -2;
 
    EnterCriticalSection(&pcore->threadlistCS);
    num = epm_arr_num(pcore->thread_list);
    for (i = 0; i < num; i++) {
        epump = epm_arr_value(pcore->thread_list, i);
        if (!epump) continue;

        epump_iodev_add(epump, pdev);

        (*epump->fdpoll)(epump, pdev);
    }
    LeaveCriticalSection(&pcore->threadlistCS);

    return 0;
}


int epcore_global_iodev_add (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
 
    if (!pcore || !pdev) return -1;
 
    EnterCriticalSection(&pcore->glbiodevlistCS);
    if (epm_arr_search(pcore->glbiodev_list, &pdev->fd, iodev_cmp_fd) != pdev)
        epm_arr_push(pcore->glbiodev_list, pdev);
    LeaveCriticalSection(&pcore->glbiodevlistCS);
 
    return 0;
}
 
int epcore_global_iodev_getmon (void * vpcore, void * veps)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    epump_t  * epump = (epump_t *)veps;
    iodev_t  * pdev = NULL;
    int        i, num;

    if (!pcore) return -1;
    if (!epump) return -2;

    EnterCriticalSection(&pcore->glbiodevlistCS);
    num = epm_arr_num(pcore->glbiodev_list);
    for (i = 0; i < num; i++) {
        pdev = epm_arr_value(pcore->glbiodev_list, i);
        if (!pdev || pdev->fd == INVALID_SOCKET) {
            epm_arr_delete(pcore->glbiodev_list, i); i--; num--;
            continue;
        }

        epump_iodev_add(epump, pdev);

        if (epump->fdpoll)
            (*epump->fdpoll)(epump, pdev);
    }
    LeaveCriticalSection(&pcore->glbiodevlistCS);
    
    return 0;
}


int epcore_global_iotimer_add (void * vpcore, void * viot)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iotimer_t * iot = (iotimer_t *)viot;
 
    if (!pcore || !iot) return -1;
 
    EnterCriticalSection(&pcore->glbiotimerlistCS);
    epm_arr_push(pcore->glbiotimer_list, iot);
    LeaveCriticalSection(&pcore->glbiotimerlistCS);
 
    return 0;
}

int epcore_global_iotimer_del (void * vpcore, void * viot)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iotimer_t * iot = (iotimer_t *)viot;
 
    if (!pcore || !iot) return -1;
 
    EnterCriticalSection(&pcore->glbiotimerlistCS);
    epm_arr_delete_ptr(pcore->glbiotimer_list, iot);
    LeaveCriticalSection(&pcore->glbiotimerlistCS);
 
    return 0;
}
 
 
int epcore_global_iotimer_getmon (void * vpcore, void * veps)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    epump_t    * epump = (epump_t *)veps;
    iotimer_t  * iot = NULL;
    int          i, num;
 
    if (!pcore) return -1;
    if (!epump) return -2;
 
    EnterCriticalSection(&pcore->glbiotimerlistCS);
    num = epm_arr_num(pcore->glbiotimer_list);
    for (i = 0; i < num; i++) {
        iot = epm_arr_delete(pcore->glbiotimer_list, i);
        if (!iot)  continue;

        iot->epump = epump;

        EnterCriticalSection(&epump->timerlistCS);
        epm_arr_insert_by(epump->timer_list, iot, iotimer_cmp_iotimer);
        LeaveCriticalSection(&epump->timerlistCS);
    }
    LeaveCriticalSection(&pcore->glbiotimerlistCS);
 
    return 0;
}



void epcore_print (void * vpcore)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    epump_t    * epump = NULL;
    int          i, num;

    if (!pcore) return;

    EnterCriticalSection(&pcore->threadlistCS);
    num = epm_arr_num(pcore->thread_list);
    printf("Total thread %d:\n", num);
    for (i = 0; i < num; i++) {
        epump = epm_arr_value(pcore->thread_list, i);
        if (!epump) continue;
 
        printf("  [Thread %d]:%lu iodev:%d iotimer:%d\n", i+1, epump->threadid,
               epump_objnum(epump, 1), epump_objnum(epump, 2));
    }
    LeaveCriticalSection(&pcore->threadlistCS);
}

