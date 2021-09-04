/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "hashtab.h"
#include "mthread.h"
#include "memory.h"
#include "bpool.h"
#include "btime.h"
#include "trace.h"

#include "epcore.h"
#include "epump_local.h"
#include "ioevent.h"
#include "iotimer.h"
#include "epwakeup.h"


int iotimer_init (void * vtimer)
{
    iotimer_t * iot = (iotimer_t *)vtimer;

    if (!iot) return -1;

    iot->res[0] = iot->res[1] = NULL;
    iot->para = NULL;
    iot->callback = NULL;
    iot->cbpara = NULL;
    iot->cmdid = 0;
    iot->threadid = 0;
    memset(&iot->bintime, 0, sizeof(iot->bintime));
    return 0;
}

void iotimer_void_free(void * vtimer)
{
    iotimer_free(vtimer);
}

int iotimer_free(void * vtimer)
{
    iotimer_t * iot = (iotimer_t *)vtimer;

    kfree(iot);
    return 0;
}

int iotimer_cmp_iotimer(void * a, void * b)
{
    iotimer_t * iota = (iotimer_t *)a;
    iotimer_t * iotb = (iotimer_t *)b;

    if (!a || !b) return -1;

    if (btime_cmp(&iota->bintime, >, &iotb->bintime)) return 1;
    if (btime_cmp(&iota->bintime, ==, &iotb->bintime)) return 0;
    return -1;
}

int iotimer_cmp_id (void * a, void * b)
{
    iotimer_t * iot = (iotimer_t *)a;
    ulong  id = *(ulong *)b;

    if (iot->id == id) return 0;
    return -1;
}

ulong iotimer_hash_func (void * key)
{
    ulong tid = *(ulong *)key;

    return tid;
}


iotimer_t * iotimer_fetch (void * vpcore)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    iotimer_t  * iot = NULL;

    if (!pcore) return NULL;

    iot = bpool_fetch(pcore->timer_pool);
    if (!iot) {
        iot = kzalloc(sizeof(*iot));
        if (!iot) return NULL;
    }

    iotimer_init(iot);

    iot->epcore = pcore;

    EnterCriticalSection(&pcore->timertableCS);
    if (pcore->timerID < 100) pcore->timerID = 100;
    iot->id = pcore->timerID++;

    ht_set(pcore->timer_table, &iot->id, iot);
    LeaveCriticalSection(&pcore->timertableCS);

    return iot;
}

int iotimer_recycle (void * viot)
{
    epcore_t  * pcore = NULL;
    epump_t   * epump = NULL;
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return -1;

    pcore = (epcore_t *)iot->epcore;
    if (!pcore) return -2;

    if (epcore_iotimer_del(pcore, iot->id) != iot)
        return 0;

    epump = (epump_t *)iot->epump;
    if (epump) {
        EnterCriticalSection(&epump->timertreeCS);
        rbtree_delete_node(epump->timer_tree, iot);
        LeaveCriticalSection(&epump->timertreeCS);
    }

    bpool_recycle(pcore->timer_pool, iot);
    return 0;
}

void * iotimer_start (void * vpcore, int ms, int cmdid, void * para, 
                       IOHandler * cb, void * cbpara)
{   
    epcore_t  * pcore = (epcore_t *)vpcore;
    epump_t   * epump = NULL;
    iotimer_t * iot = NULL;
    
    if (!pcore) return NULL;
    
    iot = iotimer_fetch(pcore);
    if (!iot) return NULL;

    iot->para = para;
    iot->cmdid = cmdid;
    btime_now_add(&iot->bintime, ms);

    iot->callback = cb;
    iot->cbpara = cbpara;

    /* assign to the caller threadid 
       indicates the current worker thread will handle the upcoming timeout event */
    if (pcore->dispmode == 1)
        iot->threadid = get_threadid();

    epump = iot->epump = epump_thread_select(pcore);
    if (!iot->epump) {
        tolog(1, "iotimer_start: %ld cmdid=%d failed to select an epump\n", iot->id, iot->cmdid);
        epcore_global_iotimer_add(pcore, iot);
        return iot;
    }

    EnterCriticalSection(&epump->timertreeCS);
    rbtree_insert(epump->timer_tree, iot, iot, NULL);
    LeaveCriticalSection(&epump->timertreeCS);

    /* if caller thread for iotimer_start is different from the epump thread,
       the epump thread shoud be waken from epoll_wait blocking. */

    if (get_threadid() != epump->threadid)
        epump_wakeup_send(epump);

    return iot;
}

int iotimer_stop (void * viot)
{
    epump_t   * epump = NULL;
    iotimer_t * iot = (iotimer_t *)viot;
    int         ret = 0;

    if (!iot) return -1;

    if (epcore_iotimer_find(iot->epcore, iot->id) != iot)
        return 0;

    epump = (epump_t *)iot->epump;

    if (epump) {

        EnterCriticalSection(&epump->timertreeCS);
        ret = rbtree_delete_node(epump->timer_tree, iot);
        LeaveCriticalSection(&epump->timertreeCS);

        if (ret >= 0) {
            /* ret >= 0 indicates that the iotimer instance not timeout,
               still in red-black tree */

            /* if caller thread for iotimer_stop is different from the epump thread,
               the epump thread shoud be waken from epoll_wait blocking. */

            if (get_threadid() != epump->threadid)
                epump_wakeup_send(epump);
        }

    } else {
        ret = epcore_global_iotimer_del(iot->epcore, iot);

        if (ret > 0) {
            /* the global timers are added to all epump threads for probing.
               ret > 0 shows that the timer instance is removed from 
               global list successfully.
             */

            epcore_wakeup_send(iot->epcore);
        }
    }

    iotimer_recycle(iot);

    return 0;
}


/* return value: if ret <=0, that indicates has no timer in
 * queue, system should set blocktime infinite,
   if ret >0, then pdiff carries the next timeout value */ 
int iotimer_check_timeout (void * vepump, btime_t * pdiff, int * pevnum)
{
    epump_t   * epump = (epump_t *)vepump;
    btime_t     systime;
    iotimer_t * iot = NULL;
    int         evnum = 0;

    if (!epump) return -1;
    if (pevnum) *pevnum = 0;

    while (1) {
        EnterCriticalSection(&epump->timertreeCS);
        iot = rbtree_min(epump->timer_tree);
        if (!iot) {
            LeaveCriticalSection(&epump->timertreeCS);
            if (pevnum) *pevnum = evnum;
            return -10;
        }

        btime(&systime);
        if (btime_cmp(&iot->bintime, <=, &systime)) {
            rbtree_delete_node(epump->timer_tree, iot);
            LeaveCriticalSection(&epump->timertreeCS);

            if (!iot) {
                if (pevnum) *pevnum = evnum;
                return -11;
            }

            PushTimeoutEvent(epump, iot);
            evnum++;
        } else {
            LeaveCriticalSection(&epump->timertreeCS);
            if (pdiff)
                *pdiff = btime_diff(&systime, &iot->bintime);
            break;
        }
    }

    if (pevnum) *pevnum = evnum;
    return 1;
}


ulong iotimer_id (void * viot)
{
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return 0;

    return iot->id;
}

int iotimer_cmdid (void * viot)
{
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return 0;

    return iot->cmdid;
}

void * iotimer_para (void * viot)
{
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return NULL;

    return iot->para;
}

void * iotimer_epump (void * viot)
{
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return NULL;

    return iot->epump;
}

ulong iotimer_workerid (void * viot)
{
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return 0;

    return iot->threadid;
}

void iotimer_workerid_set (void * viot, ulong workerid)
{
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return;

    if (workerid == 1)
        iot->threadid = get_threadid();
    else
        iot->threadid = workerid;
}

