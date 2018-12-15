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
#include "epm_hashtab.h"
#include "epm_pool.h"

#include "epcore.h"
#include "epump_internal.h"
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
    //iot->id = 0;
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

    epm_free(iot);
    return 0;
}

int iotimer_cmp_iotimer(void * a, void * b)
{
    iotimer_t * iota = (iotimer_t *)a;
    iotimer_t * iotb = (iotimer_t *)b;

    if (!a || !b) return -1;

    if (epm_time_cmp(&iota->bintime, >, &iotb->bintime)) return 1;
    if (epm_time_cmp(&iota->bintime, ==, &iotb->bintime)) return 0;
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

    iot = epm_pool_fetch(pcore->timer_pool);
    if (!iot) {
        iot = epm_zalloc(sizeof(*iot));
        if (!iot) return NULL;
    }

    iotimer_init(iot);

    iot->epcore = pcore;

    EnterCriticalSection(&pcore->timertableCS);
    iot->id = pcore->timerID++;
    if (iot->id <= 100) iot->id = pcore->timerID++;

    epm_ht_set(pcore->timer_table, &iot->id, iot);
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
        EnterCriticalSection(&epump->timerlistCS);
        epm_arr_delete_ptr(epump->timer_list, iot);
        LeaveCriticalSection(&epump->timerlistCS);
    }

    epm_pool_recycle(pcore->timer_pool, iot);
    return 0;
}

void * iotimer_start (void * vpcore, void * veps, int ms, int cmdid, void * para, 
                       IOHandler * cb, void * cbpara)
{   
    epcore_t  * pcore = (epcore_t *)vpcore;
    epump_t   * epump = (epump_t *)veps;
    iotimer_t * iot = NULL;
    
    if (!pcore) return NULL;
    
    iot = iotimer_fetch(pcore);
    if (!iot) return NULL;

    iot->para = para;
    iot->cmdid = cmdid;
    epm_time_now_add(&iot->bintime, ms);

    iot->callback = cb;
    iot->cbpara = cbpara;

    iot->epump = epump;
    if (!iot->epump) {
        iot->epump = epump = epcore_thread_self(pcore);
        if (!iot->epump)
            iot->epump = epump = epcore_thread_select(pcore);

        if (!iot->epump) {
            epcore_global_iotimer_add(pcore, iot);
            return iot;
        }
    }

    EnterCriticalSection(&epump->timerlistCS);
    epm_arr_insert_by(epump->timer_list, iot, iotimer_cmp_iotimer);
    LeaveCriticalSection(&epump->timerlistCS);

#ifdef UNIX
    if (pthread_self() != epump->threadid)
        epcore_wakeup_send(epump->epcore);
#endif
#ifdef _WIN32 
    if (GetCurrentThreadId() != epump->threadid)
        epcore_wakeup_send(epump->epcore);
#endif

    return iot;
}

int iotimer_stop (void * viot)
{
    epump_t * epump = NULL;
    iotimer_t * iot = (iotimer_t *)viot;

    if (!iot) return -1;

    if (epcore_iotimer_del(iot->epcore, iot->id) != iot)
        return 0;

    epump = (epump_t *)iot->epump;
    if (epump) {
        EnterCriticalSection(&epump->timerlistCS);
        epm_arr_delete_ptr(epump->timer_list, iot);
        LeaveCriticalSection(&epump->timerlistCS);
    } else {
        epcore_global_iotimer_del(iot->epcore, iot);
    }

    iotimer_recycle(iot);

    if (epump) {
    #ifdef UNIX
        if (pthread_self() != epump->threadid)
            epcore_wakeup_send(iot->epcore);
    #endif
    #ifdef _WIN32 
        if (GetCurrentThreadId() != epump->threadid)
            epcore_wakeup_send(iot->epcore);
    #endif
    }

#ifdef _DEBUG
printf("stop_timer: cmdid=%d id=%lu\n", iot->cmdid, iot->id);
#endif
    return 0;
}


/* return value: if ret <=0, that indicates has no timer in
 * queue, system should set blocktime infinite,
   if ret >0, then pdiff carries the next timeout value */ 
int iotimer_check_timeout (void * vepump, epm_time_t * pdiff, int * pevnum)
{
    epump_t    * epump = (epump_t *)vepump;
    epm_time_t   systime;
    iotimer_t   * iot = NULL;
    int           evnum = 0;

    if (!epump) return -1;
    if (pevnum) *pevnum = 0;

    while (1) {
        EnterCriticalSection(&epump->timerlistCS);
        iot = epm_arr_value(epump->timer_list, 0);
        if (!iot) {
            LeaveCriticalSection(&epump->timerlistCS);
            if (pevnum) *pevnum = evnum;
            return -10;
        }

        epm_time(&systime);
        if (epm_time_cmp(&iot->bintime, <=, &systime)) {
            iot = epm_arr_delete(epump->timer_list, 0);
            LeaveCriticalSection(&epump->timerlistCS);

            if (!iot) {
                if (pevnum) *pevnum = evnum;
                return -11;
            }

            PushTimeoutEvent(epump, iot);
            evnum++;
        } else {
            LeaveCriticalSection(&epump->timerlistCS);
            if (pdiff)
                *pdiff = epm_time_diff(&systime, &iot->bintime);
            break;
        }
    }

    if (pevnum) *pevnum = evnum;
    return 1;
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


