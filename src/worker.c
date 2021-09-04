/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "dynarr.h"
#include "hashtab.h"
#include "bpool.h"
#include "memory.h"
 
#include "epcore.h"
#include "worker.h"
#include "iodev.h"
#include "ioevent.h"
 
#if defined(_WIN32) || defined(_WIN64)
#include <process.h>
#endif
 
#ifdef UNIX
#include <signal.h>
#endif

void * worker_new (epcore_t * pcore)
{
    worker_t  * wker = NULL;

    wker = kzalloc(sizeof(*wker));
    if (!wker) return NULL;

    wker->epcore = pcore;
    wker->quit = 0;

    /* initialization of ioevent_t operation & management */
    InitializeCriticalSection(&wker->ioeventlistCS);
    wker->ioevent_list = lt_new();
    wker->ioevent = event_create();

    return wker;
}

void worker_free (void * vwker)
{
    worker_t  * wker = (worker_t *)vwker;
    ioevent_t * ioe = NULL;

    if (!wker) return;

    /* free all ioevent instances and clean the ioevent_t facilities */
    EnterCriticalSection(&wker->ioeventlistCS);
    while ((ioe = lt_rm_head(wker->ioevent_list)) != NULL) {
        ioevent_free(ioe);
    }
    lt_free(wker->ioevent_list);
    wker->ioevent_list = NULL;
    LeaveCriticalSection(&wker->ioeventlistCS);
 
    event_set(wker->ioevent, -10);
    DeleteCriticalSection(&wker->ioeventlistCS);
    event_destroy(wker->ioevent);
    wker->ioevent = NULL;

    kfree(wker);
}

int worker_cmp_threadid (void * a, void * b)
{
    worker_t  * worker = (worker_t *)a;
    ulong      threadid = *(ulong *)b;
 
    if (!worker) return -1;
 
    if (worker->threadid > threadid) return 1;
    if (worker->threadid < threadid) return -1;
    return 0;
}

int worker_cmp_worker_by_eventnum (void * a, void * b)
{
    worker_t * epsa = *(worker_t **)a;
    worker_t * epsb = *(worker_t **)b;
 
    if (!epsa || !epsb) return -1;
 
    return worker_ioevent_num(epsa) - worker_ioevent_num(epsb);
}
 
int worker_cmp_worker_by_load (void * a, void * b)
{
    worker_t * epsa = *(worker_t **)a;
    worker_t * epsb = *(worker_t **)b;
 
    if (!epsa || !epsb) return -1;
 
    //return worker_real_load(epsa) - worker_real_load(epsb);
    return epsa->workload - epsb->workload;
}


ulong workerid (void * veps)
{
    worker_t  * worker = (worker_t *)veps;
 
    if (!worker) return 0;
 
    return worker->threadid;
}
 
/* worker load is calculated by 2 factors: 
   the first one is the ioevent number pending in waiting-queue, 
   the weigth is about 70%.
   the second one is working time ratio, the weight is 30%. */

int worker_real_load (void * vwker)
{
    worker_t  * wker = (worker_t *)vwker;
    epcore_t  * pcore = NULL;
    int         num, total = 0;
    int         load = 0;

    if (!wker) return 0;

    pcore = (epcore_t *)wker->epcore;
    if (!pcore) return 0;

    num = lt_num(wker->ioevent_list);
    total = bpool_fetched_num(pcore->event_pool);
    if (total > 0)
        load = (double)num / (double)total * 600.;

    load += wker->working_ratio * 300.;

    if (pcore->acc_event_num > 0)
        load += (double) wker->acc_event_num / (double)pcore->acc_event_num * 100.;

    wker->workload = load;

    return load;
}

void worker_perf (void * vwker, ulong * acctime, 
                  ulong * idletime, ulong * worktime, ulong * eventnum)
{
    worker_t  * wker = (worker_t *)vwker;
    btime_t     curt;

    if (!wker) return;

    if (acctime) {
        btime(&curt);
        *acctime = btime_diff_ms(&wker->start_time, &curt);
    }

    if (idletime) *idletime = wker->acc_idle_time;
    if (worktime) *worktime = wker->acc_working_time;
    if (eventnum) *eventnum = wker->acc_event_num;
}


int worker_ioevent_num (void * vwker)
{
    worker_t  * worker = (worker_t *)vwker;
    int         num = 0;
 
    if (!worker) return 0;
 
    EnterCriticalSection(&worker->ioeventlistCS);
    num = lt_num(worker->ioevent_list);
    LeaveCriticalSection(&worker->ioeventlistCS);

    return num;
}
 

int worker_ioevent_push (void * vwker, void * vioe)
{
    worker_t  * wker = (worker_t *)vwker;
    ioevent_t * ioe = (ioevent_t *)vioe;
    ioevent_t * ioetmp = NULL;
    int         discard = 0;

    if (!wker) return -1;
    if (!ioe) return -2;
 
    EnterCriticalSection(&wker->ioeventlistCS);
 
    ioetmp = lt_first(wker->ioevent_list);
    while (ioetmp) {
        if (ioe->obj == ioetmp->obj && ioe->obj != NULL &&
            ioe->type == ioetmp->type &&
            ioe->callback == ioetmp->callback &&
            ioe->cbpara == ioetmp->cbpara)
        {
            discard = 1;
            break;
        }

        ioetmp = lt_get_next(ioetmp);
    }

    if (!discard) {
        time(&ioe->stamp);
        lt_append(wker->ioevent_list, ioe);
    }
 
    LeaveCriticalSection(&wker->ioeventlistCS);

    /* wakeup the worker to handle the ioevent */
    if (!discard)
        event_set(wker->ioevent, 100);

    if (discard) {
        bpool_recycle(wker->epcore->event_pool, ioe);
    }

    return 0;
}

void * worker_ioevent_pop (void * vworker)
{
    worker_t   * worker = (worker_t *)vworker;
    ioevent_t  * ioe = NULL;
 
    EnterCriticalSection(&worker->ioeventlistCS);
    ioe = lt_rm_head(worker->ioevent_list);
    LeaveCriticalSection(&worker->ioeventlistCS);

    return ioe;
}

int worker_ioevent_remove (void * vworker, void * obj)
{
    worker_t   * worker = (worker_t *)vworker;
    epcore_t   * pcore = NULL;
    ioevent_t  * ioe = NULL;
    ioevent_t  * ioetmp = NULL;
    int          num = 0;
 
    if (!worker) return -1;

    pcore = worker->epcore;
    if (!pcore) return -2;

    EnterCriticalSection(&worker->ioeventlistCS);
    ioe = lt_first(worker->ioevent_list);
    while (ioe) {
        ioetmp = lt_get_next(ioe);
        if (ioe->obj == obj) {
            lt_delete_ptr(worker->ioevent_list, ioe);
            bpool_recycle(pcore->event_pool, ioe);
            num++;
        }
        ioe = ioetmp;
    }
    LeaveCriticalSection(&worker->ioeventlistCS);

    return num;
}


int worker_main_proc (void * vwker)
{
    worker_t  * wker = (worker_t *)vwker;
    epcore_t  * pcore = NULL;
    ioevent_t * ioe = NULL;
    btime_t     t0, t1;
    int         diff = 0;
    int         exenum = 0;

    if (!wker) return -1;

    pcore = (epcore_t *)wker->epcore;
    if (!pcore) goto end_worker;

    wker->threadid = get_threadid();

    worker_thread_add(pcore, wker);

    btime(&wker->start_time);
    wker->count_tick = wker->start_time;
    t0 = wker->start_time;

    while (!pcore->quit && !wker->quit) {

        if (worker_ioevent_num(wker) <= 0) {
            /* calculate the worker load before sleeping */
            worker_real_load(wker);

            event_wait(wker->ioevent, 5*1000);

            /* calcualte load again when waking up */
            worker_real_load(wker);
            exenum = 0;

            if (pcore->quit || wker->quit)
                break;
        }
        exenum++;

        btime(&t1);
        wker->acc_idle_time += btime_diff_ms(&t0, &t1);

        ioe = worker_ioevent_pop(wker);
        if (ioe) {

            wker->curioe = ioe;
            ioevent_execute(pcore, ioe);
            wker->curioe = NULL;

            wker->acc_event_num++;
    
            /* if there is no event followed the iodev_t, 
               workerid can be set 0. This leads that subsequent
               ioevent generated by the iodev_t will be dispatched 
               to different workers in balance.*/
        }
    
        btime(&t0);
        diff = btime_diff_ms(&t1, &t0);
        wker->acc_working_time += diff;
        wker->working_time += diff;

        /* calculate the ratio of working time every 10 seconds */

        if ((diff = btime_diff_ms(&wker->count_tick, &t0)) >= 10000) {
            wker->working_ratio = (double)wker->working_time / (double)diff;
            wker->count_tick = t0;
            wker->working_time = 0;

            /* working ratio value changing demands calcualting of  load */
            worker_real_load(wker);

        } else {
            if (exenum % 10 == 0)
                worker_real_load(wker);
        }
    }

end_worker:
    worker_thread_del(pcore, wker);
    worker_free(wker);

    return 0;
}


 
#if defined(_WIN32) || defined(_WIN64)
unsigned WINAPI worker_proc_entry (void * arg)
{
#endif
#ifdef UNIX
void * worker_proc_entry (void * arg)
{
    sigset_t sigmask;
    int      ret = 0;

    pthread_detach(pthread_self());

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGPIPE);
    ret = pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    if (ret != 0) {
#ifdef _DEBUG
        printf("worker: block sigpipe error\n");
#endif
    }
#endif
 
    worker_main_proc (arg);
 
#ifdef UNIX
    return NULL;
#endif
#if defined(_WIN32) || defined(_WIN64)
    return 0;
#endif
}
 
 
int worker_main_start (void * vpcore, int forkone)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    worker_t  * wker = NULL;
#if defined(_WIN32) || defined(_WIN64)
    HANDLE      hpth;
    unsigned    thid;
#endif
#ifdef UNIX
    pthread_attr_t attr;
    pthread_t  thid;
    int        ret = 0;
#endif
 
    if (!pcore) return -1;
 
    wker = worker_new(pcore);
    if (!wker) return -100;

    if (!forkone) {
         worker_proc_entry(wker);
         return 0;
    }
 
#if defined(_WIN32) || defined(_WIN64)
    hpth = (HANDLE)_beginthreadex(
                                NULL,
                                0,
                                worker_proc_entry,
                                wker,
                                0,
                                &thid);
    if (hpth == NULL) {
        return -101;
    }
#endif
 
#ifdef UNIX
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
 
    do {
        ret = pthread_create(&thid, &attr,
                             worker_proc_entry, wker);
    } while (ret != 0);
 
    pthread_detach(thid);
    //pthread_join(thid, NULL);
#endif
 
    return 0;
}


void worker_main_stop (void * vwker)
{
    worker_t  * wker = (worker_t *)vwker;

    if (!wker) return;

    wker->quit = 1;

    event_set(wker->ioevent, -10);
}

