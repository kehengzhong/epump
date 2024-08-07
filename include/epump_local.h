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

#ifndef _EPUMP_LOCAL_H_
#define _EPUMP_LOCAL_H_

#include "btype.h"
#include "dlist.h"
#include "hashtab.h"
#include "dynarr.h"
#include "mthread.h"
#include "rbtree.h"

#ifdef HAVE_EPOLL
#include <sys/epoll.h>

#elif defined(HAVE_KQUEUE)
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
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
  #elif defined(HAVE_KQUEUE)
    int                kqueue_fd;        /* kqueue file descriptor */
    int                kqueue_size;      /* maximum concurrent FD for monitoring, get from conf */
    struct kevent    * kqueue_events;
  #else
    CRITICAL_SECTION   fdsetCS;
    fd_set             readFds;         /* read file descriptor set */
    fd_set             writeFds;        /* write file descriptor set */
    fd_set             exceptionFds;    /* exception file descriptor set */
  #endif 

    fd_set_poll      * setpoll;
    fd_del_poll      * delpoll;
    fd_dispatch      * fddispatch;

    /* wake up the epoll_wait/select while waiting in block for the fd-set ready */
#ifdef HAVE_EVENTFD
    int                wakeupfd;
    void             * wakeupdev;
#endif

    uint8              epumpsleep;

    /* Store all devices that need event monitoring in the current ePump thread.
       The same device object may be added to the device_tree in multiple ePump,
       so the alloc_node parameter must be set to 1 when creating the device_tree. */
    CRITICAL_SECTION   devicetreeCS;
    rbtree_t         * device_tree;

    /* Manage the timer triggered by the current ePump thread, and the timer instances
       are sorted according to the timeout time and timer ID. The global timer will be
       added to the timer_tree in multiple ePump. The alloc_node must be set 1 also. */
    CRITICAL_SECTION   timertreeCS;
    rbtree_t         * timer_tree;

    /* ePump monitors the FD list for read-write readiness and timer timeout.
       When read-write readiness or timer timeout occurs, it creates events
       such as readable, writable, connected or timeout, and adds the ioevent_t
       events to the following queue. */
    CRITICAL_SECTION   ioeventlistCS;
    dlist_t          * ioevent_list;
    void             * curioe;

    /* external event register management */
    CRITICAL_SECTION   exteventlistCS;
    arr_t            * exteventlist;
    int                exteventindex;

    /* current threads management */
    ulong              threadid;
#if defined(_WIN32) || defined(_WIN64)
    HANDLE             epumphandle;
#endif

    uint8              quit;
    uint8              blocking;
    int                deblock_times;

    epcore_t         * epcore;
} epump_t, *epump_p;


void * epump_new (epcore_t * epcore);
int    epump_free (void * vepump);

int    epcore_epump_free (void * vepump);

int  epump_init (void * vpump);
void epump_recycle (void * vpump);

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
void   epump_iodev_print (void * vepump, int printtype);
SOCKET epump_iodev_maxfd (void * vepump);
int    epump_iodev_scan  (void * vepump);

int  epump_main_start (void * vpcore, int forkone);
void epump_main_stop (void * vepump);

#ifdef __cplusplus
}   
#endif

#endif

