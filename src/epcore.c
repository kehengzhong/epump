/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "dynarr.h"
#include "hashtab.h"
#include "btime.h"
#include "mthread.h"
#include "memory.h"
#include "bpool.h"
#include "tsock.h"

#include "epcore.h"
#include "epump_local.h"
#include "worker.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epwakeup.h"
#include "mlisten.h"
#include "epdns.h"

#ifdef HAVE_IOCP
#include "epiocp.h"
#endif

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


void * epcore_new (int maxfd, int dispmode)
{
    epcore_t * pcore = NULL;
#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsd;
#endif

#if defined(_WIN32) || defined(_WIN64)
    if (WSAStartup (MAKEWORD(2,2), &wsd) != 0) {
        return NULL;
    }
#endif

    pcore = kzalloc(sizeof(*pcore));
    if (!pcore) return NULL;

    if (maxfd <= 1024) maxfd = 65535 * 8;
    pcore->maxfd = maxfd;
    set_fd_limit(maxfd);

    pcore->dispmode = dispmode;

    time(&pcore->startup_time);
    pcore->quit = 0;

#ifdef HAVE_EVENTFD
    pcore->wakeupfd = -1;
#else
    pcore->informport = 0;
    pcore->informfd = INVALID_SOCKET;
    pcore->wakeupfd = INVALID_SOCKET;
#endif
    pcore->wakeupdev = NULL;

#ifdef HAVE_IOCP
    epcore_iocp_init(pcore);
#endif

    /* initialize memory pool resource */
    if (!pcore->device_pool) {
        pcore->device_pool = bpool_init(NULL);
        bpool_set_initfunc(pcore->device_pool, iodev_init);
        bpool_set_freefunc(pcore->device_pool, iodev_free);
        bpool_set_unitsize(pcore->device_pool, sizeof(iodev_t));
        bpool_set_allocnum(pcore->device_pool, 256);
    }

    if (!pcore->timer_pool) {
        pcore->timer_pool = bpool_init(NULL);
        bpool_set_freefunc(pcore->timer_pool, iotimer_free);
        bpool_set_unitsize(pcore->timer_pool, sizeof(iotimer_t));
        bpool_set_allocnum(pcore->timer_pool, 256);
    }

    if (!pcore->event_pool) {
        pcore->event_pool = bpool_init(NULL);
        bpool_set_freefunc(pcore->event_pool, ioevent_free);
        bpool_set_unitsize(pcore->event_pool, sizeof(ioevent_t));
        bpool_set_allocnum(pcore->event_pool, 64);
    }

    if (!pcore->epump_pool) {
        pcore->epump_pool = bpool_init(NULL);
        bpool_set_freefunc(pcore->epump_pool, epump_free);
        bpool_set_unitsize(pcore->epump_pool, sizeof(epump_t));
        bpool_set_allocnum(pcore->epump_pool, 8);
    }

    /* initialization of IODevice operation & management */
    InitializeCriticalSection(&pcore->devicetableCS);
    pcore->device_table = ht_only_new(300000, iodev_cmp_id);
    ht_set_hash_func(pcore->device_table, iodev_hash_func);
    pcore->deviceID = 100;

    /* initialization of IOTimer operation & management */
    InitializeCriticalSection(&pcore->timertableCS);
    pcore->timer_table = ht_only_new(300000, iotimer_cmp_id);
    ht_set_hash_func(pcore->timer_table, iotimer_hash_func);
    pcore->timerID = 100;

    InitializeCriticalSection(&pcore->glbiodevlistCS);
    pcore->glbiodev_list = arr_new(16);

    InitializeCriticalSection(&pcore->glbiotimerlistCS);
    pcore->glbiotimer_list = arr_new(32);

    InitializeCriticalSection(&pcore->epumplistCS);
    pcore->curpump = 0;
    pcore->epump_list = arr_new(32);
    pcore->epump_tab = ht_only_new(300, epump_cmp_threadid);

    InitializeCriticalSection(&pcore->workerlistCS);
    pcore->curwk = 0;
    pcore->worker_list = arr_new(64);
    pcore->worker_tab = ht_only_new(300, worker_cmp_threadid);

    InitializeCriticalSection(&pcore->eventnumCS);
    pcore->acc_event_num = 0;

    epcore_mlisten_init(pcore);
    epcore_wakeup_init(pcore);

    pcore->dnsmgmt = dns_mgmt_init(pcore, NULL, NULL);

    return pcore;
}


void epcore_clean (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;

    if (!pcore) return;

    dns_mgmt_clean(pcore->dnsmgmt);

    epcore_mlisten_clean(pcore);

    epcore_stop_epump(pcore);
    epcore_stop_worker(pcore);
    SLEEP(50);

    epcore_wakeup_clean(pcore);

    /* clean the IODevice facilities */
    DeleteCriticalSection(&pcore->devicetableCS);
    ht_free_all(pcore->device_table, iodev_free);
    pcore->device_table = NULL;

    /* clean the IOTimer facilities */
    DeleteCriticalSection(&pcore->timertableCS);
    ht_free_all(pcore->timer_table, iotimer_free);
    pcore->timer_table = NULL;

    /* free the global iodev, iotimer, listen port list */
    DeleteCriticalSection(&pcore->glbiodevlistCS);
    arr_free(pcore->glbiodev_list);
    pcore->glbiodev_list = NULL;

    DeleteCriticalSection(&pcore->glbiotimerlistCS);
    arr_free(pcore->glbiotimer_list);
    pcore->glbiotimer_list = NULL;

    /* free the resource of epump thread management */
    DeleteCriticalSection(&pcore->epumplistCS);
    arr_free(pcore->epump_list);
    pcore->epump_list = NULL;
    ht_free(pcore->epump_tab);
    pcore->epump_tab = NULL;

    /* free the resource of worker thread management */
    DeleteCriticalSection(&pcore->workerlistCS);
    arr_free(pcore->worker_list);
    pcore->worker_list = NULL;
    ht_free(pcore->worker_tab);
    pcore->worker_tab = NULL;

    DeleteCriticalSection(&pcore->eventnumCS);

#ifdef HAVE_IOCP
    epcore_iocp_clean(pcore);
#endif

    /* release all memory pool resource */
    bpool_clean(pcore->timer_pool);
    bpool_clean(pcore->device_pool);
    bpool_clean(pcore->event_pool);
    bpool_clean(pcore->epump_pool);

    kfree(pcore);

#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif
}

int epcore_dnsrv_add (void * vpcore, char * nsip, int port)
{
    epcore_t  * pcore = (epcore_t *)vpcore;

    if (!pcore) return -1;

    return dns_nsrv_append (pcore->dnsmgmt, nsip, port);
}


int epcore_set_callback (void * vpcore, void * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;

    if (!pcore) return -1;

    pcore->callback = (IOHandler *)cb;
    pcore->cbpara = cbpara;

    return 0;
}

 
void epcore_start_epump (void * vpcore, int maxnum)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    int         i = 0;

    if (!pcore) return;

    if (maxnum < 1) return;

    for (i = 0; i < maxnum; i++) {
        epump_main_start(pcore, 1);
    }

    //epump_main_start(pcore, 0);
}

void epcore_stop_epump (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *)vpcore;

    pcore->quit = 1;
    epcore_wakeup_send(pcore);
}


void epcore_start_worker (void * vpcore, int maxnum)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    int         i = 0;
 
    if (!pcore) return;
    if (maxnum < 1) return;
 
    for (i = 0; i < maxnum; i++) {
        worker_main_start(pcore, 1);
    }
}
 
void epcore_stop_worker (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    worker_t  * wker = NULL;
    int         i, num;
 
    if (!pcore) return;

    EnterCriticalSection(&pcore->workerlistCS);

    num = arr_num(pcore->worker_list);
    for (i = 0; i < num; i++) {
        wker = arr_value(pcore->worker_list, i);

        worker_main_stop(wker);
    }

    LeaveCriticalSection(&pcore->workerlistCS);
}
 


int epcore_iodev_add (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
 
    if (!pcore || !pdev) return -1;
 
    EnterCriticalSection(&pcore->devicetableCS);
    ht_set(pcore->device_table, &pdev->id, pdev);
    LeaveCriticalSection(&pcore->devicetableCS);
 
    return 0;
}
 
void * epcore_iodev_del (void * vpcore, ulong id)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
 
    if (!pcore) return NULL;
 
    EnterCriticalSection(&pcore->devicetableCS);
    pdev = ht_delete(pcore->device_table, &id);
    LeaveCriticalSection(&pcore->devicetableCS);
 
    return pdev;
}

void * epcore_iodev_find (void * vpcore, ulong id)
{
    epcore_t * pcore = (epcore_t *) vpcore;
    iodev_t  * pdev = NULL;
 
    if (!pcore) return NULL;
 
    EnterCriticalSection(&pcore->devicetableCS);
    pdev = ht_get(pcore->device_table, &id);
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
    num = ht_num(pcore->device_table);
    for (i=0; i<num; i++) {
        pdev = ht_value(pcore->device_table, i);
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
    ht_set(pcore->timer_table, &iot->id, iot);
    LeaveCriticalSection(&pcore->timertableCS);

    return 0;
}

void * epcore_iotimer_del (void * vpcore, ulong id)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    iotimer_t  * iot = NULL;

    if (!pcore) return NULL;

    EnterCriticalSection(&pcore->timertableCS);
    iot = ht_delete(pcore->timer_table, &id);
    LeaveCriticalSection(&pcore->timertableCS);

    return iot;
}

void * epcore_iotimer_find (void * vpcore, ulong id)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    iotimer_t * iot = NULL;

    if (!pcore) return NULL;

    EnterCriticalSection(&pcore->timertableCS);
    iot = ht_get(pcore->timer_table, &id);
    LeaveCriticalSection(&pcore->timertableCS);

    return iot;
}


int epump_thread_add (void * vpcore, void * vepump)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    epump_t   * epump = (epump_t *)vepump;

    if (!pcore) return -1;
    if (!epump) return -2;

    EnterCriticalSection(&pcore->epumplistCS);
    if (ht_get(pcore->epump_tab, &epump->threadid) != epump) {
        ht_set(pcore->epump_tab, &epump->threadid, epump);
        arr_push(pcore->epump_list, epump);
    }
    LeaveCriticalSection(&pcore->epumplistCS);

    return 0;
}

int epump_thread_del (void * vpcore, void * vepump)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    epump_t   * epump = (epump_t *)vepump;
         
    if (!pcore) return -1; 
    if (!epump) return -2;
         
    EnterCriticalSection(&pcore->epumplistCS);
    if (ht_delete(pcore->epump_tab, &epump->threadid) == epump)
        arr_delete_ptr(pcore->epump_list, epump);
    LeaveCriticalSection(&pcore->epumplistCS);
 
    return 0; 
}

void * epump_thread_find (void * vpcore, ulong threadid)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    epump_t   * epump = NULL;
 
    if (!pcore) return NULL;
 
    EnterCriticalSection(&pcore->epumplistCS);
    epump = ht_get(pcore->epump_tab, &threadid);
    LeaveCriticalSection(&pcore->epumplistCS);
 
    return epump;
}

int epump_thread_sort (void * vpcore, int type)
{  
    epcore_t  * pcore = (epcore_t *) vpcore; 
           
    if (!pcore) return -1;  
          
    if (type == 1) 
        arr_sort_by(pcore->epump_list, epump_cmp_epump_by_devnum);
    else if (type == 2)
        arr_sort_by(pcore->epump_list, epump_cmp_epump_by_timernum);
    else
        arr_sort_by(pcore->epump_list, epump_cmp_epump_by_objnum);
    time(&pcore->epump_sorttime);
     
    return 0;  
}

void * epump_thread_self (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore; 
    epump_t   * epump = NULL;
    ulong       threadid = 0;
           
    if (!pcore) return NULL;  

    threadid = get_threadid();

    EnterCriticalSection(&pcore->epumplistCS);
    epump = ht_get(pcore->epump_tab, &threadid);
    LeaveCriticalSection(&pcore->epumplistCS);

    return epump;
}


void * epump_thread_select (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore; 
    epump_t   * epump = NULL;
           
    if (!pcore) return NULL;  

    EnterCriticalSection(&pcore->epumplistCS);

    if (time(NULL) - pcore->epump_sorttime > 5) {
        epump_thread_sort(pcore, 3);
        pcore->curpump = 0;
    } else {
        if (++pcore->curpump >= arr_num(pcore->epump_list))
            pcore->curpump = 0;
    }

    epump = arr_value(pcore->epump_list, pcore->curpump);

    LeaveCriticalSection(&pcore->epumplistCS);

    return epump;
}

int epump_thread_setpoll (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
    epump_t   * epump = NULL;
    int         i, num;
 
    if (!pcore) return -1;
    if (!pdev) return -2;
 
    EnterCriticalSection(&pcore->epumplistCS);

    num = arr_num(pcore->epump_list);
    for (i = 0; i < num; i++) {

        epump = arr_value(pcore->epump_list, i);
        if (!epump) continue;

        epump_iodev_add(epump, pdev);

        (*epump->setpoll)(epump, pdev);
    }

    LeaveCriticalSection(&pcore->epumplistCS);

    return 0;
}

int epump_thread_delpoll (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
    epump_t   * epump = NULL;
    int         i, num;
 
    if (!pcore) return -1;
    if (!pdev) return -2;
 
    EnterCriticalSection(&pcore->epumplistCS);

    num = arr_num(pcore->epump_list);
    for (i = 0; i < num; i++) {

        epump = arr_value(pcore->epump_list, i);
        if (!epump) continue;
 
        if (epump_iodev_del(epump, pdev->fd) != NULL)
            (*epump->delpoll)(epump, pdev);
    }

    LeaveCriticalSection(&pcore->epumplistCS);
 
    return 0;
}
 

 
int worker_thread_add (void * vpcore, void * vworker)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    worker_t  * worker = (worker_t *)vworker;
 
    if (!pcore) return -1;
    if (!worker) return -2;
 
    EnterCriticalSection(&pcore->workerlistCS);
    if (ht_get(pcore->worker_tab, &worker->threadid) != worker) {
        ht_set(pcore->worker_tab, &worker->threadid, worker);
        arr_push(pcore->worker_list, worker);
    }
    LeaveCriticalSection(&pcore->workerlistCS);
 
    return 0;
}
 
int worker_thread_del (void * vpcore, void * vworker)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    worker_t  * worker = (worker_t *)vworker;
 
    if (!pcore) return -1;
    if (!worker) return -2;
 
    EnterCriticalSection(&pcore->workerlistCS);
    if (ht_delete(pcore->worker_tab, &worker->threadid) == worker)
        arr_delete_ptr(pcore->worker_list, worker);
    LeaveCriticalSection(&pcore->workerlistCS);
 
    return 0;
}
 
void * worker_thread_find (void * vpcore, ulong threadid)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    worker_t  * wker = NULL;
 
    if (!pcore) return NULL;
 
    EnterCriticalSection(&pcore->workerlistCS);
    wker = ht_get(pcore->worker_tab, &threadid);
    LeaveCriticalSection(&pcore->workerlistCS);
 
    return wker;
}


int worker_thread_sort (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
 
    if (!pcore) return -1;
 
    arr_sort_by(pcore->worker_list, worker_cmp_worker_by_load);
    time(&pcore->worker_sorttime);
 
    return 0;
}
 
void * worker_thread_self (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    worker_t  * worker = NULL;
    ulong       threadid = 0;
 
    if (!pcore) return NULL;
 
    threadid = get_threadid();
 
    EnterCriticalSection(&pcore->workerlistCS);
    worker = ht_get(pcore->worker_tab, &threadid);
    LeaveCriticalSection(&pcore->workerlistCS);
 
    return worker;
}
 
 
void * worker_thread_select (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *) vpcore;
    worker_t  * worker = NULL;
 
    if (!pcore) return NULL;
 
    /* select one lowest-load worker from the worker list.
       the load is decided by pending ioevent number and working time ratio */

    EnterCriticalSection(&pcore->workerlistCS);

    /* if comment following if line, the dispatching of IOEvent will be more balanced.
       but overheads will also rise. commented on 2020-4-30 00:15:00 */

    if (time(NULL) - pcore->worker_sorttime >= 10) {
        worker_thread_sort(pcore);
        pcore->curwk = 0;
    } else {
        if (++pcore->curwk >= arr_num(pcore->worker_list))
            pcore->curwk = 0;
    }

    worker = arr_value(pcore->worker_list, pcore->curwk);

    LeaveCriticalSection(&pcore->workerlistCS);
 
    return worker;
}


int epcore_global_iodev_add (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
 
    if (!pcore || !pdev) return -1;
 
    EnterCriticalSection(&pcore->glbiodevlistCS);
    if (arr_search(pcore->glbiodev_list, &pdev->fd, iodev_cmp_fd) != pdev)
        arr_push(pcore->glbiodev_list, pdev);
    LeaveCriticalSection(&pcore->glbiodevlistCS);
 
    return 0;
}
 
int epcore_global_iodev_del (void * vpcore, void * vpdev)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = (iodev_t *)vpdev;
 
    if (!pcore || !pdev) return -1;
 
    EnterCriticalSection(&pcore->glbiodevlistCS);
    arr_delete_ptr(pcore->glbiodev_list, pdev);
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

    num = arr_num(pcore->glbiodev_list);
    for (i = 0; i < num; i++) {

        pdev = arr_value(pcore->glbiodev_list, i);
        if (!pdev || pdev->fd == INVALID_SOCKET) {
            arr_delete(pcore->glbiodev_list, i); i--; num--;
            continue;
        }

        if (epump_iodev_find(epump, pdev->fd) != NULL) continue;

        if (pdev->bindtype != BIND_ALL_EPUMP) {
            arr_delete(pcore->glbiodev_list, i); i--; num--;
            pdev->epump = epump;
        }

        epump_iodev_add(epump, pdev);

        if (epump->setpoll)
            (*epump->setpoll)(epump, pdev);
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
    arr_push(pcore->glbiotimer_list, iot);
    LeaveCriticalSection(&pcore->glbiotimerlistCS);
 
    return 0;
}

int epcore_global_iotimer_del (void * vpcore, void * viot)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iotimer_t * iot = (iotimer_t *)viot;
    int         ret = 0;
 
    if (!pcore || !iot) return -1;
 
    EnterCriticalSection(&pcore->glbiotimerlistCS);
    if (arr_delete_ptr(pcore->glbiotimer_list, iot) != NULL)
        ret = 1;
    LeaveCriticalSection(&pcore->glbiotimerlistCS);
 
    return ret;
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

    num = arr_num(pcore->glbiotimer_list);
    for (i = 0; i < num; i++) {

        iot = arr_delete(pcore->glbiotimer_list, i);
        if (!iot)  continue;

        iot->epump = epump;

        EnterCriticalSection(&epump->timertreeCS);
        rbtree_insert(epump->timer_tree, iot, iot, NULL);
        LeaveCriticalSection(&epump->timertreeCS);
    }

    LeaveCriticalSection(&pcore->glbiotimerlistCS);
 
    return 0;
}

void epcore_print (void * vpcore)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    epump_t    * epump = NULL;
    worker_t   * wker = NULL;
    int          i, num;

    if (!pcore) return;

    EnterCriticalSection(&pcore->epumplistCS);
    num = arr_num(pcore->epump_list);
    printf("Total epump thread %d:\n", num);
    for (i = 0; i < num; i++) {
        epump = arr_value(pcore->epump_list, i);
        if (!epump) continue;
 
        printf("  [Thread %d]:%lu iodev:%d iotimer:%d\n", i+1, epump->threadid,
               epump_objnum(epump, 1), epump_objnum(epump, 2));
    }
    LeaveCriticalSection(&pcore->epumplistCS);

    EnterCriticalSection(&pcore->workerlistCS);
    num = arr_num(pcore->worker_list);
    printf("Total worker thread %d:\n", num);
    for (i = 0; i < num; i++) {
        wker = arr_value(pcore->worker_list, i);
        if (!wker) continue;
 
        printf("  [Thread %d]:%lu idle_time:%lu working_time:%lu working_ratio:%.3f "
               "execute_event:%lu pending_event:%d\n",
               i+1, wker->threadid, wker->acc_idle_time,
               wker->acc_working_time, wker->working_ratio,
               wker->acc_event_num, lt_num(wker->ioevent_list));
    }
    LeaveCriticalSection(&pcore->workerlistCS);
}

