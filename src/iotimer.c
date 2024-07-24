/*
 * Copyright (c) 2003-2024 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 *
 * #####################################################
 * #                       _oo0oo_                     #
 * #                      o8888888o                    #
 * #                      88" . "88                    #
 * #                      (| -_- |)                    #
 * #                      0\  =  /0                    #
 * #                    ___/`---'\___                  #
 * #                  .' \\|     |// '.                #
 * #                 / \\|||  :  |||// \               #
 * #                / _||||| -:- |||||- \              #
 * #               |   | \\\  -  /// |   |             #
 * #               | \_|  ''\---/''  |_/ |             #
 * #               \  .-\__  '-'  ___/-. /             #
 * #             ___'. .'  /--.--\  `. .'___           #
 * #          ."" '<  `.___\_<|>_/___.'  >' "" .       #
 * #         | | :  `- \`.;`\ _ /`;.`/ -`  : | |       #
 * #         \  \ `_.   \_ __\ /__ _/   .-` /  /       #
 * #     =====`-.____`.___ \_____/___.-`___.-'=====    #
 * #                       `=---='                     #
 * #     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   #
 * #               佛力加持      佛光普照              #
 * #  Buddha's power blessing, Buddha's light shining  #
 * #####################################################
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
    iot->res[2] = iot->res[3] = NULL;

    iot->cmdid = 0;
    iot->id = 0;
    iot->para = NULL;
    memset(&iot->bintime, 0, sizeof(iot->bintime));

    iot->callback = NULL;
    iot->cbpara = NULL;

    iot->epump = NULL;
    iot->threadid = 0;
    return 0;
}

int epcore_iotimer_free (void * vtimer)
{
    iotimer_t * iot = (iotimer_t *)vtimer;
    epcore_t  * pcore = NULL;

    if (!iot) return -1;

    pcore = iot->epcore;
    if (!pcore) return -2;

    if (iotimer_free(iot) == 0)
        mpool_recycle(pcore->timer_pool, iot);

    return 0;
}

void iotimer_void_free(void * vtimer)
{
    iotimer_free(vtimer);
}

int iotimer_free(void * vtimer)
{
    return 0;
}

int iotimer_cmp_iotimer(void * a, void * b)
{
    iotimer_t * iota = (iotimer_t *)a;
    iotimer_t * iotb = (iotimer_t *)b;

    if (!a || !b) return -1;

    if (btime_cmp(&iota->bintime, >, &iotb->bintime)) return 1;
    if (btime_cmp(&iota->bintime, <, &iotb->bintime)) return -1;

    if (iota->id > iotb->id) return 1;
    if (iota->id < iotb->id) return -1;

    return 0;
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

    iot = mpool_fetch(pcore->timer_pool);
    if (!iot) {
        return NULL;
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

int iotimer_recycle (void * vpcore, ulong iotid)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iotimer_t * iot = NULL;
    epump_t   * epump = NULL;

    if (!pcore) return -1;

    if ((iot = epcore_iotimer_del(pcore, iotid)) == NULL)
        return 0;

    epump = (epump_t *)iot->epump;
    if (epump) {
        EnterCriticalSection(&epump->timertreeCS);
        rbtree_delete(epump->timer_tree, iot);
        LeaveCriticalSection(&epump->timertreeCS);
    }

    mpool_recycle(pcore->timer_pool, iot);
    return 0;
}

void * iotimer_start(void * vpcore, int ms, int cmdid, void * para, 
                     IOHandler * cb, void * cbpara, ulong epumpid)
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
    iot->threadid = get_threadid();

    if (epumpid < 10) epumpid = iot->threadid;
    if (epumpid > 10)
        epump = iot->epump =  epump_thread_get(pcore, epumpid);

    if (!iot->epump) {
        epcore_global_iotimer_add(pcore, iot);
        return (void *)iot->id;
    }

    EnterCriticalSection(&epump->timertreeCS);
    rbtree_insert(epump->timer_tree, iot, iot, NULL);
    LeaveCriticalSection(&epump->timertreeCS);

    /* if caller thread for iotimer_start is different from the epump thread,
       the epump thread shoud be waken from epoll_wait blocking. */

    if (get_threadid() != epump->threadid)
        epump_wakeup_send(epump);

    return (void *)iot->id;
}

int iotimer_stop_dbg (void * vpcore, void * iotid, char * file, int line)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    epump_t   * epump = NULL;
    iotimer_t * iot = NULL;
    iotimer_t * iter = NULL;
    int         ret = 0;

    if (!pcore) return -1;

    iot = epcore_iotimer_del(pcore, (ulong)iotid);
    if (!iot || iot->id != (ulong)iotid) {
        tolog(1, "TimClo: %s iotid=%lu iot->id=%lu %s:%d\n",
              iot==NULL?"NotFound":"IDError", (ulong)iotid, iot?iot->id:0, file, line);
        return 0;
    }

    epump = (epump_t *)iot->epump;

    if (epump) {

        EnterCriticalSection(&epump->timertreeCS);
        iter = rbtree_delete(epump->timer_tree, iot);
        LeaveCriticalSection(&epump->timertreeCS);

        if (iter != NULL) {
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

    mpool_recycle(pcore->timer_pool, iot);
    return 0;
}

void epump_iotimer_print (void * vepump, int printtype)
{
    epump_t    * epump = (epump_t *) vepump;
    iotimer_t  * iot = NULL;
    int          i, num, iter = 0;
    rbtnode_t  * rbtn = NULL;
    char         buf[32768];

    if (!epump) return;

    EnterCriticalSection(&epump->timertreeCS);

    rbtn = rbtree_min_node(epump->timer_tree);
    num = rbtree_num(epump->timer_tree);

    sprintf(buf, " ePump:%lu TimerNum=%d :", epump->threadid, num);
    iter = strlen(buf);

    for (i = 0; i < num && rbtn; i++) {
        iot = RBTObj(rbtn);
        rbtn = rbtnode_next(rbtn);

        if (!iot) continue;

        sprintf(buf+iter, " %ld.%ld/%d/%lu",
                iot->bintime.s%10000, iot->bintime.ms, iot->cmdid, iot->id);
        iter = strlen(buf);
        if (iter > sizeof(buf) - 16) break;
    }
    if (printtype == 0) printf("%s\n", buf);
    else if (printtype == 1) tolog(1, "%s\n", buf);

    LeaveCriticalSection(&epump->timertreeCS);
}


/* return value: if ret <=0, that indicates has no timer in
 * queue, system should set blocktime infinite,
   if ret >0, then pdiff carries the next timeout value */ 
int iotimer_check_timeout (void * vepump, btime_t * pdiff, int * pevnum)
{
    epump_t   * epump = (epump_t *)vepump;
    btime_t     systime;
    rbtnode_t * rbtn = NULL;
    iotimer_t * iot = NULL;
    int         evnum = 0;

    if (pevnum) *pevnum = 0;
    if (!epump) return -1;

    while (1) {
        EnterCriticalSection(&epump->timertreeCS);
        rbtn = rbtree_min_node(epump->timer_tree);
        if (rbtn == NULL || (iot = RBTObj(rbtn)) == NULL) {
            LeaveCriticalSection(&epump->timertreeCS);
            if (pevnum) *pevnum = evnum;
            return -10;
        }

        btime(&systime);
        if (btime_cmp(&iot->bintime, <=, &systime)) {
            if (rbtree_delete_node(epump->timer_tree, rbtn) < 0)
                epump_iotimer_print(epump, 1);
            LeaveCriticalSection(&epump->timertreeCS);

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
    return evnum > 0 ? 1 : 0;
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

ulong iotimer_epumpid (void * viot)
{
    iotimer_t * iot = (iotimer_t *)viot;
    epump_t   * epump = NULL;

    if (!iot) return 0;

    epump = iot->epump;
    return epump ? epump->threadid : 0;
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

