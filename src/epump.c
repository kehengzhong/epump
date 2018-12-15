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

#include "epcore.h"
#include "epump_internal.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epwakeup.h"

#ifdef _WIN32
#include <process.h>
#endif
 
#ifdef HAVE_EPOLL
#include "epepoll.h"
#else
#include "epselect.h"
#endif


void * epump_new (epcore_t * pcore)
{
    epump_t * epump = NULL;

    epump = epm_zalloc(sizeof(*epump));
    if (!epump) return NULL;

    epump->epcore = pcore;
    epump->quit = 0;

    epump->blocking = 0;
    epump->deblock_times = 0;

#ifdef HAVE_EPOLL
    if (epump_epoll_init(epump, pcore->maxfd) < 0) {
        epm_free(epump);
        return NULL;
    }
    epump->fdpoll = epump_epoll_setpoll;
    epump->fdpollclear = epump_epoll_clearpoll;
    epump->fddispatch = epump_epoll_dispatch;
#else
    if (epump_select_init(epump) < 0) {
        epm_free(epump);
        return NULL;
    }
    epump->fdpoll = epump_select_setpoll;
    epump->fdpollclear = epump_select_clearpoll;
    epump->fddispatch = epump_select_dispatch;
#endif

    /* initialization of IODevice operation & management */
    InitializeCriticalSection(&epump->devicetableCS);
#ifdef HAVE_EPOLL
    if (epump->epoll_size > 2000) {
        epump->device_table = epm_ht_only_new(epump->epoll_size, iodev_cmp_fd);
    } else {
        epump->device_table = epm_ht_only_new(2000, iodev_cmp_fd);
    }
    epm_ht_set_hash_func(epump->device_table, iodev_hash_fd_func);
#else
    epump->device_list = epm_arr_new(128);
#endif


    /* initialization of IOTimer operation & management */
    InitializeCriticalSection(&epump->timerlistCS);
    epump->timer_list = epm_arr_new(128);

    /* initialization of ioevent_t operation & management */
    InitializeCriticalSection(&epump->ioeventlistCS);
    epump->ioevent_list = epm_lt_new();
    epump->ioevent = event_create();

    InitializeCriticalSection(&epump->exteventlistCS);
    epump->exteventlist = epm_arr_new(16);
    epump->exteventindex = 0;

    return epump;
}


void epump_free (void * vepump)
{
    epump_t * epump = (epump_t *)vepump;
    ioevent_t * ioe = NULL;

    if (!epump) return;

    /*epump->quit = 1;
    epcore_wakeup_send(epump->epcore);
    SLEEP(10);*/
    
    /* clean the IODevice facilities */
    DeleteCriticalSection(&epump->devicetableCS);
#ifdef HAVE_EPOLL
    epm_ht_free(epump->device_table);
    epump->device_table = NULL;
#else
    epm_arr_free(epump->device_list);
    epump->device_list = NULL;
    DeleteCriticalSection(&epump->fdsetCS);
#endif

    /* clean the IOTimer facilities */
    DeleteCriticalSection(&epump->timerlistCS);
    epm_arr_free(epump->timer_list);
    epump->timer_list = NULL;
 

    /* clean the ioevent_t facilities */
    EnterCriticalSection(&epump->ioeventlistCS);
    while ((ioe = epm_lt_rm_head(epump->ioevent_list)) != NULL) {
        ioevent_free(ioe);
    }
    epm_lt_free(epump->ioevent_list);
    epump->ioevent_list = NULL;
    LeaveCriticalSection(&epump->ioeventlistCS);
 
    event_set(epump->ioevent, -10);
    DeleteCriticalSection(&epump->ioeventlistCS);
    event_destroy(epump->ioevent);
    epump->ioevent = NULL;

    /* clean external events */
    DeleteCriticalSection(&epump->exteventlistCS);
    while (epm_arr_num(epump->exteventlist) > 0) {
        ioevent_free(epm_arr_pop(epump->exteventlist));
    }
    epm_arr_free(epump->exteventlist);
    epump->exteventlist = NULL;

#ifdef HAVE_EPOLL
    epump_epoll_clean(epump);
#else
    epump_select_clean(epump);
#endif

    epm_free(epump);
}


int extevent_cmp_type (void * a, void * b)
{
    ioevent_t * ioe = (ioevent_t *)a;
    uint16      type = *(uint16 *)b;

    if (ioe->type > type) return 1;
    if (ioe->type == type) return 0;

    return -1;
}

int epump_hook_register (void * vepump, void * ignitor, void * igpara, 
                           void * callback, void * cbpara)
{
    epump_t   * epump = (epump_t *) vepump;
    ioevent_t * ioe = NULL;
    uint16      type = IOE_USER_DEFINED;
    int         i, num = 0;

    if (!epump) return -1;

    EnterCriticalSection(&epump->exteventlistCS);
    num = epm_arr_num(epump->exteventlist);
    for (i=0; i<num; i++) {
        ioe = epm_arr_value(epump->exteventlist, i);
        if (!ioe || ioe->externflag != 1) continue;
        if (ioe->ignitor == ignitor && ioe->igpara == igpara && 
            ioe->callback == callback && ioe->obj == cbpara) 
        {
            LeaveCriticalSection(&epump->exteventlistCS);
            return 0;
        }
    }
    LeaveCriticalSection(&epump->exteventlistCS);

    for (type = IOE_USER_DEFINED; type < 65535; type++) {
        EnterCriticalSection(&epump->exteventlistCS);
        ioe = epm_arr_search(epump->exteventlist, &type, extevent_cmp_type);
        LeaveCriticalSection(&epump->exteventlistCS);
        if (!ioe) break;
    }
    if (type >= 65535) return -100;

    ioe = (ioevent_t *)epm_pool_fetch(epump->epcore->event_pool);
    if (!ioe) return -10;

    ioe->externflag = 1;
    ioe->type = type;
    ioe->ignitor = ignitor;
    ioe->igpara = igpara;
    ioe->callback = callback;
    ioe->obj = cbpara;

    EnterCriticalSection(&epump->exteventlistCS);
    epm_arr_push(epump->exteventlist, ioe);
    LeaveCriticalSection(&epump->exteventlistCS);

    return 0;
}

int epump_hook_remove (void * vepump, void * ignitor, void * igpara, 
                         void * callback, void * cbpara)
{
    epump_t   * epump = (epump_t *) vepump;
    ioevent_t * ioe = NULL;
    int         found = 0;
    int         i, num = 0;

    if (!epump) return -1;

    EnterCriticalSection(&epump->exteventlistCS);
    num = epm_arr_num(epump->exteventlist);
    for (i=0; i<num; i++) {
        ioe = epm_arr_value(epump->exteventlist, i);
        if (!ioe || ioe->externflag != 1) continue;
        if (ioe->ignitor == ignitor && ioe->igpara == igpara && 
            ioe->callback == callback && ioe->obj == cbpara) 
        {
            ioe = epm_arr_delete(epump->exteventlist, i);
            found = 1;
            break;
        }
        ioe = NULL;
    }
    LeaveCriticalSection(&epump->exteventlistCS);

    if (found && ioe) epm_pool_recycle(epump->epcore->event_pool, ioe);
    return found;
}


int epump_cmp_threadid (void * a, void * b)
{
    epump_t  * epump = (epump_t *)a;
    ulong      threadid = *(ulong *)b;

    if (!epump) return -1;

    if (epump->threadid > threadid) return 1;
    if (epump->threadid < threadid) return -1;
    return 0;
}

int epump_cmp_epump_by_objnum (void * a, void * b)
{
    epump_t * epsa = *(epump_t **)a;
    epump_t * epsb = *(epump_t **)b;

    if (!epsa || !epsb) return -1;

    return epump_objnum(epsa, 0) - epump_objnum(epsb, 0);
}

int epump_cmp_epump_by_devnum (void * a, void * b)
{
    epump_t * epsa = *(epump_t **)a;
    epump_t * epsb = *(epump_t **)b;

    if (!epsa || !epsb) return -1;

    return epump_objnum(epsa, 1) - epump_objnum(epsb, 1);
}

int epump_cmp_epump_by_timernum (void * a, void * b)
{
    epump_t * epsa = *(epump_t **)a;
    epump_t * epsb = *(epump_t **)b;

    if (!epsa || !epsb) return -1;

    return epump_objnum(epsa, 2) - epump_objnum(epsb, 2);
}

ulong epumpid (void * veps)
{
    epump_t  * epump = (epump_t *)veps;

    if (!epump) return 0;

    return epump->threadid;
}

int epump_objnum (void * veps, int type)
{
    epump_t  * epump = (epump_t *)veps;
    int        devnum = 0;
    int        timernum = 0;

    if (!epump) return 0;

  #ifdef HAVE_EPOLL
    devnum = epm_ht_num(epump->device_table);
  #else
    devnum = epm_arr_num(epump->device_list);
  #endif
    timernum = epm_arr_num(epump->timer_list);

    if (type == 1) return devnum;
    if (type == 2) return timernum;
    return devnum + timernum;
}


int epump_iodev_add (void * veps, void * vpdev)
{
    epump_t  * epump = (epump_t *)veps;
    iodev_t  * pdev = (iodev_t *)vpdev;

    if (!epump || !pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) return -2;

    EnterCriticalSection(&epump->devicetableCS);

  #ifdef HAVE_EPOLL
    epm_ht_set(epump->device_table, &pdev->fd, pdev);
  #else
    if (arr_find_by(epump->device_list, &pdev->fd, iodev_cmp_fd) != pdev)
        epm_arr_insert_by(epump->device_list, pdev, iodev_cmp_iodev);
  #endif

    LeaveCriticalSection(&epump->devicetableCS);

    return 0;
}

void * epump_iodev_del (void * veps, SOCKET fd)
{
    epump_t * epump = (epump_t *) veps;
    iodev_t * pdev = NULL;

    if (!epump) return NULL;

    if (fd == INVALID_SOCKET) return NULL;

    EnterCriticalSection(&epump->devicetableCS);

  #ifdef HAVE_EPOLL
    pdev = epm_ht_delete(epump->device_table, &fd);
  #else
    pdev = epm_arr_delete_by(epump->device_list, &pdev->fd, iodev_cmp_fd);
  #endif
    LeaveCriticalSection(&epump->devicetableCS);

    return pdev;
}


void * epump_iodev_find (void * vepump, SOCKET fd)
{
    epump_t  * epump = (epump_t *) vepump;
    iodev_t  * pdev = NULL;
 
    if (!epump) return NULL;
 
    EnterCriticalSection(&epump->devicetableCS);
  #ifdef HAVE_EPOLL
    pdev = epm_ht_get(epump->device_table, &fd);
  #else
    pdev = epm_arr_find_by(epump->device_list, &fd, iodev_cmp_fd);
  #endif
    LeaveCriticalSection(&epump->devicetableCS);
 
    return pdev;
}
 
int epump_iodev_tcpnum (void * vepump)
{
    epump_t  * epump = (epump_t *) vepump;
    iodev_t  * pdev = NULL;
    int        i, num, retval = 0;
 
    if (!epump) return 0;
 
    EnterCriticalSection(&epump->devicetableCS);
  #ifdef HAVE_EPOLL
    num = epm_ht_num(epump->device_table);
    for (i=0; i<num; i++) {
        pdev = epm_ht_value(epump->device_table, i);
  #else
    num = epm_arr_num(epump->device_list);
    for (i=0; i<num; i++) {
        pdev = epm_arr_value(epump->device_list, i);
  #endif
        if (!pdev) continue;
        if (pdev->fdtype == FDT_CONNECTED || pdev->fdtype == FDT_ACCEPTED)
            retval++;
    }
    LeaveCriticalSection(&epump->devicetableCS);
 
    return retval;
}

 
SOCKET epump_iodev_maxfd (void * vepump)
{
#ifdef HAVE_EPOLL
    return 0;
#else
    epump_t * epump = (epump_t *) vepump;
    SOCKET    maxfd = 0;
    iodev_t * pdev = NULL;
    int       i, num;
 
    if (!epump) return 0;
 
    EnterCriticalSection(&epump->devicetableCS);
    num = epm_arr_num(epump->device_list);
    for (i=0; i<num; i++) {
        pdev = (iodev_t *)arr_value(epump->device_list, i);
        if (!pdev) {
            epm_arr_delete(epump->device_list, i);
            continue;
        }
        if (pdev->fd == INVALID_SOCKET) {
            continue;
        }
        if (maxfd < pdev->fd + 1)
            maxfd = pdev->fd + 1;
        break;
    }
    LeaveCriticalSection(&epump->devicetableCS);
 
    return maxfd;
#endif
}
 
int epump_device_scan (void * vepump)
{
#ifdef HAVE_EPOLL
    return 0;
#else
    epump_t   * epump = (epump_t *) vepump;
    iodev_t    * pdev = NULL;
    int             i, num;
    fd_set          rFds, wFds;
    struct timeval  timeout, *tout;
    int             nFds = 0;
 
    EnterCriticalSection(&epump->devicetableCS);
    FD_ZERO(&epump->readFds);
    FD_ZERO(&epump->writeFds);
 
    num = epm_arr_num(epump->device_list);
    for (i=0; i<num; i++) {
        pdev = epm_arr_value(epump->device_list, i);
        if (!pdev) {
            epm_arr_delete(epump->device_list, i);
            continue;
        }
 
        FD_ZERO(&rFds);
        FD_ZERO(&wFds);
 
        timeout.tv_sec = timeout.tv_usec = 0;
        tout = &timeout;
 
        if (pdev->rwflag & RWF_READ) {
            if (pdev->fd != INVALID_SOCKET) {
                FD_SET(pdev->fd, &rFds);
                nFds = select (1, &rFds, NULL, NULL, tout);
            } else {
                nFds = SOCKET_ERROR;
            }
            if (nFds == SOCKET_ERROR /* && errno == EBADF*/) {
#ifdef _DEBUG
debug_info("[Monitor]: read FD=%d socket error, errno=%d %s.\n",
            pdev->fd, errno, strerror(errno));
#endif
                /* clean up */
                FD_CLR (pdev->fd, &rFds);
                pdev->rwflag &= ~RWF_READ;
 
                PushInvalidDevEvent(epump, pdev);
            } else {
                FD_SET(pdev->fd, &epump->readFds);
            }
        }
        if (pdev->rwflag & RWF_WRITE) {
            if (pdev->fd >= 0) {
                FD_SET(pdev->fd, &wFds);
                nFds = select (1, NULL, &wFds, NULL, tout);
            } else {
                nFds = SOCKET_ERROR;
            }
            if (nFds == SOCKET_ERROR /*&& errno == EBADF*/) {
#ifdef _DEBUG
debug_info("[Monitor]: write FD=%d socket error, errno=%d %s.\n", pdev->fd, errno, strerror(errno));
#endif
                /* clean up */
                FD_CLR (pdev->fd, &wFds);
                pdev->rwflag &= ~RWF_WRITE;
 
                PushInvalidDevEvent(epump, pdev);
            } else {
                FD_SET(pdev->fd, &epump->writeFds);
            }
        }
    }
    LeaveCriticalSection(&epump->devicetableCS);
 
    return 0;
#endif
}
 

int epump_main_proc (void * veps)
{
    epump_t   * epump = (epump_t *)veps;
    epcore_t  * pcore = NULL;
    int         ret = 0;
    int         evnum = 0;
    epm_time_t  diff, * pdiff = NULL;

    if (!epump) return -1;

#ifdef UNIX
    epump->threadid = pthread_self();
#endif
#ifdef _WIN32
    epump->threadid = GetCurrentThreadId();
#endif

    pcore = (epcore_t *)epump->epcore;
    if (!pcore) return -2;

    epcore_thread_add(pcore, epump);

    /* now append the global fd's in epcore to current epoll-fd monitoring list */
    epcore_wakeup_getmon(pcore, epump);
    epcore_global_iodev_getmon(pcore, epump);
    epcore_global_iotimer_getmon(pcore, epump);

#ifdef _DEBUG
printf("Thread: %lu entering bigloop\n", epump->threadid);
#endif

    while (pcore->quit == 0) {
        /* check timer list whether timeout events occur */
        ret = iotimer_check_timeout(epump, &diff, &evnum);

        /* handle all events in event queue via invoking their callback */
        ioevent_handle(epump);

        do {
            ret = iotimer_check_timeout(epump, &diff, &evnum);
            if (evnum > 0) ioevent_handle(epump);
        } while (evnum > 0);

        if (pcore->quit) break;

        /* waiting for the readiness notification from the monitored fd-list */
        if (ret <= 0) pdiff = NULL; else pdiff = &diff;
        (*epump->fddispatch)(epump, pdiff);
    }

#ifdef _DEBUG
printf("Thread: %lu exited bigloop\n", epump->threadid);
#endif

    epcore_thread_del(pcore, epump);
    epump_free(epump);

    return 0;
}


 
#ifdef _WIN32
unsigned WINAPI epump_main_thread (void * arg)
{
#endif
#ifdef UNIX
void * epump_main_thread (void * arg)
{
#endif

    pthread_detach(pthread_self());
    epump_main_proc(arg);

#ifdef UNIX
    return NULL;
#endif
#ifdef _WIN32
    return 0;
#endif
}


int epump_main_start (void * vpcore, int forkone)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    epump_t   * epump = NULL;
#ifdef _WIN32
    unsigned    thid;
#endif
#ifdef UNIX
    pthread_attr_t attr;
    pthread_t  thid;
    int        ret = 0;
#endif
 
    if (!pcore) return -1;

    epump = epump_new(pcore);
    if (!epump) return -100;

    if (!forkone) return epump_main_proc(epump);

#ifdef _WIN32
    pcore->epumphandle = (HANDLE)_beginthreadex(
                                NULL,
                                0,
                                epump_main_thread,
                                epump,
                                0,
                                &thid);
    if (epump->epumphandle == NULL) {
        epump_free(epump);
        return -101;
    }
#endif
 
#ifdef UNIX
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
 
    do {
        ret = pthread_create(&thid, &attr, epump_main_thread, epump);
    } while (ret != 0);
 
    pthread_detach(thid);
    //pthread_join(thid, NULL);
#endif
 
    return 0;
}

