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

#ifndef _EPCORE_H_
#define _EPCORE_H_

#include "btype.h"
#include "mthread.h"
#include "dynarr.h"
#include "hashtab.h"
#include "bpool.h"
#include "mpool.h"
#include "btime.h"
#include "frame.h"

#ifdef __cplusplus      
extern "C" {           
#endif

typedef int IOHandler (void * vmgmt, void * pobj, int event, int fdtype);
typedef int GeneralCB (void * vpara, int status);
typedef int fd_set_poll (void * vepump, void * vpdev);
typedef int fd_del_poll (void * vepump, void * vpdev);
typedef int fd_dispatch (void * vepump, btime_t * delay);


typedef struct EPCore_ {

    int                maxfd;

#ifdef HAVE_IOCP
    HANDLE             iocp_port;
#endif

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
    hashtab_t        * device_table;
    ulong              deviceID;

    CRITICAL_SECTION   timertableCS;
    hashtab_t        * timer_table;
    ulong              timerID;

    CRITICAL_SECTION   glbiodevlistCS;
    arr_t            * glbiodev_list;
    CRITICAL_SECTION   glbiotimerlistCS;
    arr_t            * glbiotimer_list;
    CRITICAL_SECTION   glbmlistenlistCS;
    arr_t            * glbmlisten_list;

    /* An ePump instance corresponds to an ePump thread, and the ePump thread
       calls epoll/select/kqueue and other mechanisms to monitor the read-write
       readiness of the file descriptor and the timeout state of the timer,
       generate an event, and execute the callback function corresponding to
       the event, and so on. The system creates ePump threads with the corresponding
       number of CPU cores to make efficient use of concurrent computing power. */
    CRITICAL_SECTION   epumplistCS;
    int                curpump;
    arr_t            * epump_list;
    hashtab_t        * epump_tab;
    time_t             epump_sorttime;

    /* A worker_t instance corresponds to a worker thread, and the worker thread
       blocks to wait for the event queue and calls the callback function to handle
       the events in the event queue of the worker_t instance. Events are created
       by the ePump thread and posted to the event queue of the specified worker
       instance according to the binding relationship. It is generally recommended
       that the system does not create a worker thread, and the ePump thread is
       responsible for both event monitoring and event handling. */
    CRITICAL_SECTION   workerlistCS;
    int                curwk;
    arr_t            * worker_list;
    hashtab_t        * worker_tab;
    time_t             worker_sorttime;

    CRITICAL_SECTION   eventnumCS;
    ulong              acc_event_num;

    /* default handler of all event generated during runtime */
    IOHandler        * callback;
    void             * cbpara;
 
    /* memory pool management */
    mpool_t          * device_pool;
    mpool_t          * timer_pool;
    mpool_t          * event_pool;
    mpool_t          * epump_pool;
    mpool_t          * devrbn_pool;
    mpool_t          * timrbn_pool;

    /* DNS management instance */
    void             * dnsmgmt;

    /* configuration file API visiting handle */
    void             * hconf;

    uint8              quit;
    time_t             startup_time;

} epcore_t, *epcore_p;


void * epcore_new (int maxfd);
void   epcore_clean (void * vpcore);

int    epcore_dnsrv_add (void * vpcore, char * nsip, int port);

void   epcore_start_epump (void * vpcore, int maxnum);
void   epcore_stop_epump (void * vpcore);

void   epcore_start_worker (void * vpcore, int maxnum);
void   epcore_stop_worker (void * vpcore);

int    epcore_set_callback (void * vpcore, void * cb, void * cbpara);

int    epcore_iodev_add (void * vpcore, void * vpdev);
void * epcore_iodev_del (void * vpcore, ulong id);
void * epcore_iodev_find (void * vpcore, ulong id);
int    epcore_iodev_tcpnum (void * vpcore);

int    epcore_iotimer_add (void * vpcore, void * vpdev);
void * epcore_iotimer_del (void * vpcore, ulong id);
void * epcore_iotimer_find (void * vpcore, ulong id);


/* system may create multiple epump threads to check or signify
   the event of file-descriptors. */

int    epump_thread_add (void * vpcore, void * vepump);
int    epump_thread_del (void * vpcore, void * vepump);
void * epump_thread_find (void * vpcore, ulong threadid);
void * epump_thread_get  (void * vpcore, ulong threadid);
void * epump_thread_self (void * vpcore);
void * epump_thread_select (void * vpcore);

int    epump_thread_setpoll (void * vpcore, void * vpdev);
int    epump_thread_delpoll (void * vpcore, void * vpdev);


int    worker_thread_add (void * vpcore, void * vworker);
int    worker_thread_del (void * vpcore, void * vworker);
void * worker_thread_find (void * vpcore, ulong threadid);
void * worker_thread_self (void * vpcore);
void * worker_thread_select (void * vpcore);


int epcore_global_iodev_add (void * vpcore, void * vpdev);
int epcore_global_iodev_del (void * vpcore, void * vpdev);
int epcore_global_iodev_getmon (void * vpcore, void * veps);

int epcore_global_iotimer_add (void * vpcore, void * viot);
int epcore_global_iotimer_del (void * vpcore, void * viot);
int epcore_global_iotimer_getmon (void * vpcore, void * veps);

void epcore_print (void * vpcore, frame_p frm, FILE * fp);

#ifdef __cplusplus
}   
#endif


#endif

