/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifndef _WORKER_H_
#define _WORKER_H_

#include "btype.h"
#include "dlist.h"
#include "hashtab.h"
#include "dynarr.h"
#include "mthread.h"

#ifdef __cplusplus      
extern "C" {           
#endif


typedef struct Worker_s {
    void             * res[2];

    /* epumps thread will constantly dispatch io-event into worker event-list.
     * the worker thread waits for and consumes the events by executing their handler */
    CRITICAL_SECTION   ioeventlistCS;
    dlist_t          * ioevent_list;
    void             * ioevent;
    void             * curioe;

    /* current threads management */
    ulong              threadid;
#if defined(_WIN32) || defined(_WIN64)
    HANDLE             hworker;
#endif

    btime_t            start_time;
    ulong              acc_idle_time;
    ulong              acc_working_time;
    ulong              acc_event_num;

    btime_t            count_tick;
    ulong              working_time;
    double             working_ratio;
    int                event_num;

    int                workload;

    epcore_t         * epcore;
    uint8              quit;

} worker_t, *worker_p;


void * worker_new (epcore_t * epcore);
void   worker_free (void * vwker);

int worker_cmp_threadid (void * a, void * b);
int worker_cmp_worker_by_eventnum (void * a, void * b);
int worker_cmp_worker_by_load (void * a, void * b);

ulong  workerid (void * vwker);

/* worker load is calculated by 2 factors:  
   the first one is the ioevent number pending in waiting-queue,  
   the weigth is about 70%.
   the second one is working time ratio, the weight is 30%. */
int worker_real_load (void * vwker);

void worker_perf (void * vwker, ulong * acctime,
                  ulong * idletime, ulong * worktime, ulong * eventnum);


int    worker_ioevent_num (void * veps);
int    worker_ioevent_push (void * vwker, void * ioe);
void * worker_ioevent_pop (void * vworker);
int    worker_ioevent_remove (void * vworker, void * obj);

int worker_main_start (void * vpcore, int forkone);
void worker_main_stop (void * vwker);


#ifdef __cplusplus
}   
#endif

#endif

