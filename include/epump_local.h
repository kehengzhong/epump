/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
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

    /* manage all the device being probed in select_set 
     * the instance stored in the list is from DeviceDesc */
    CRITICAL_SECTION   devicetreeCS;
    rbtree_t         * device_tree;

    /* all the timer instance arranged in a sorted list */
    CRITICAL_SECTION   timertreeCS;
    rbtree_t         * timer_tree;

    /* when select returned, some fd read/write event occur. probing system
     * will generate some reading or writing events to fill into the event list.
     * and set the event to signal state to wake up all the blocking thread. */
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
void   epump_free (void * vepump);

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
SOCKET epump_iodev_maxfd (void * vepump);
int    epump_iodev_scan  (void * vepump);

int  epump_main_start (void * vpcore, int forkone);
void epump_main_stop (void * vepump);

#ifdef __cplusplus
}   
#endif

#endif

