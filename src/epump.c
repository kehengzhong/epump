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
#include "dynarr.h"
#include "hashtab.h"
#include "bpool.h"
#include "memory.h"
#include "rbtree.h"
#include "trace.h"
 
#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epwakeup.h"
#include "mlisten.h"
 
#if defined(_WIN32) || defined(_WIN64)
#include <process.h>
#endif
 
#ifdef HAVE_EPOLL
#include "epepoll.h"
#elif defined(HAVE_KQUEUE)
#include "epkqueue.h"
#elif defined(HAVE_IOCP)
#include "epiocp.h"
#elif defined(HAVE_SELECT)
#include "epselect.h"
#endif
 
 
void * epump_new (epcore_t * pcore)
{
    epump_t * epump = NULL;
 
    epump = mpool_fetch(pcore->epump_pool);
    if (!epump) return NULL;
 
    epump->epcore = pcore;
    epump->quit = 0;
 
    epump->blocking = 0;
    epump->deblock_times = 0;
 
#ifdef HAVE_EPOLL
    if (epump_epoll_init(epump, pcore->maxfd) < 0) {
        mpool_recycle(pcore->epump_pool, epump);
        return NULL;
    }
    epump->setpoll = epump_epoll_setpoll;
    epump->delpoll = epump_epoll_clearpoll;
    epump->fddispatch = epump_epoll_dispatch;
 
#elif defined(HAVE_KQUEUE)
    if (epump_kqueue_init(epump, pcore->maxfd) < 0) {
        mpool_recycle(pcore->epump_pool, epump);
        return NULL;
    }
    epump->setpoll = epump_kqueue_setpoll;
    epump->delpoll = epump_kqueue_clearpoll;
    epump->fddispatch = epump_kqueue_dispatch;
 
#elif defined(HAVE_IOCP)
    if (epump_iocp_init(epump, pcore->maxfd) < 0) {
        mpool_recycle(pcore->epump_pool, epump);
        return NULL;
    }
    epump->setpoll = epump_iocp_setpoll;
    epump->delpoll = epump_iocp_clearpoll;
    epump->fddispatch = epump_iocp_dispatch;

#elif defined(HAVE_SELECT)
    if (epump_select_init(epump) < 0) {
        mpool_recycle(pcore->epump_pool, epump);
        return NULL;
    }
    epump->setpoll = epump_select_setpoll;
    epump->delpoll = epump_select_clearpoll;
    epump->fddispatch = epump_select_dispatch;
#endif
 
    epump->epumpsleep = 0;

    /* A device such as listen device, may be associated with multiple ePump threads,
       so a device object may be added to the RBTree in multiple ePumps. Therefore,
       the device_tree in ePump must set alloc_node parameter to 1 when it is created,
       and the memory at the beginning of iodev_t cannot be reused as the RBTree node pointer. */
    InitializeCriticalSection(&epump->devicetreeCS);
    if (epump->device_tree == NULL)
        epump->device_tree = rbtree_alloc(iodev_cmp_fd, 1, 0, NULL, pcore->devrbn_pool);
 
    /* initialization of IOTimer operation & management */
    InitializeCriticalSection(&epump->timertreeCS);
    if (epump->timer_tree == NULL)
        epump->timer_tree = rbtree_alloc(iotimer_cmp_iotimer, 1, 0, NULL, pcore->timrbn_pool);
 
    /* initialization of ioevent_t operation & management */
    InitializeCriticalSection(&epump->ioeventlistCS);
    if (epump->ioevent_list == NULL)
        epump->ioevent_list = lt_new();
 
    InitializeCriticalSection(&epump->exteventlistCS);
    if (epump->exteventlist == NULL)
        epump->exteventlist = arr_new(16);
    epump->exteventindex = 0;
 
    return epump;
}
 
int epcore_epump_free (void * vepump)
{
    epump_t   * epump = (epump_t *)vepump;
    epcore_t  * pcore = NULL;

    if (!epump) return -1;

    pcore = epump->epcore;
    if (!pcore) return -2;

    if (epump_free(epump) == 0)
        mpool_recycle(pcore->epump_pool, epump);

    return 0;
}
 
int epump_free (void * vepump)
{
    epump_t   * epump = (epump_t *)vepump;
    ioevent_t * ioe = NULL;
 
    if (!epump) return -1;
 
    epump_wakeup_clean(epump);
 
    /* clean the IODevice facilities */
    DeleteCriticalSection(&epump->devicetreeCS);
 
    if (epump->device_tree) {
        rbtree_free(epump->device_tree);
        epump->device_tree = NULL;
    }
 
    if (epump->timer_tree) {
        /* clean the IOTimer facilities */
        DeleteCriticalSection(&epump->timertreeCS);
        rbtree_free(epump->timer_tree);
        epump->timer_tree = NULL;
    }
 
    /* clean the ioevent_t facilities */
    if (epump->ioevent_list) {
        EnterCriticalSection(&epump->ioeventlistCS);
        while ((ioe = lt_rm_head(epump->ioevent_list)) != NULL) {
            ioevent_free(ioe);
        }
        lt_free(epump->ioevent_list);
        epump->ioevent_list = NULL;
        LeaveCriticalSection(&epump->ioeventlistCS);
    }
 
    DeleteCriticalSection(&epump->ioeventlistCS);
 
    /* clean external events */
    if (epump->exteventlist) {
        DeleteCriticalSection(&epump->exteventlistCS);
        while (arr_num(epump->exteventlist) > 0) {
            ioevent_free(arr_pop(epump->exteventlist));
        }
        arr_free(epump->exteventlist);
        epump->exteventlist = NULL;
    }
 
#ifdef HAVE_EPOLL
    epump_epoll_clean(epump);
#elif defined(HAVE_KQUEUE)
    epump_kqueue_clean(epump);
#elif defined(HAVE_IOCP)
    epump_iocp_clean(epump);
#elif defined(HAVE_SELECT)
    epump_select_clean(epump);
#endif
 
    return 0;
}
 
int epump_init (void * vepump)
{
    epump_t * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    epump->quit = 0;
 
    return 0;
}
 
void epump_recycle (void * vepump)
{
    epump_t   * epump = (epump_t *)vepump;
    epcore_t  * pcore = NULL;
 
    if (!epump) return;
 
    pcore = (epcore_t *)epump->epcore;
    if (!pcore) {
        epump_free(epump);
        return;
    }
 
    epump->quit = 1;

    mpool_recycle(pcore->epump_pool, epump);
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
    num = arr_num(epump->exteventlist);
    for (i=0; i<num; i++) {
        ioe = arr_value(epump->exteventlist, i);
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
        ioe = arr_search(epump->exteventlist, &type, extevent_cmp_type);
        LeaveCriticalSection(&epump->exteventlistCS);
        if (!ioe) break;
    }
    if (type >= 65535) return -100;
 
    ioe = (ioevent_t *)mpool_fetch(epump->epcore->event_pool);
    if (!ioe) return -10;
 
    ioe->externflag = 1;
    ioe->type = type;
    ioe->ignitor = ignitor;
    ioe->igpara = igpara;
    ioe->callback = callback;
    ioe->obj = cbpara;
 
    EnterCriticalSection(&epump->exteventlistCS);
    arr_push(epump->exteventlist, ioe);
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
    num = arr_num(epump->exteventlist);
    for (i = 0; i < num; i++) {
        ioe = arr_value(epump->exteventlist, i);
        if (!ioe || ioe->externflag != 1) continue;
        if (ioe->ignitor == ignitor && ioe->igpara == igpara && 
            ioe->callback == callback && ioe->obj == cbpara) 
        {
            ioe = arr_delete(epump->exteventlist, i);
            found = 1;
            break;
        }
        ioe = NULL;
    }
    LeaveCriticalSection(&epump->exteventlistCS);
 
    if (found && ioe) {
        mpool_recycle(epump->epcore->event_pool, ioe);
    }

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
 
    devnum = rbtree_num(epump->device_tree);
 
    timernum = rbtree_num(epump->timer_tree);
 
    if (type == 1) return devnum;
    if (type == 2) return timernum;
 
    return devnum + timernum;
}
 
 
int epump_iodev_add (void * veps, void * vpdev)
{
#ifdef HAVE_IOCP
    return 0;
#else
    epump_t  * epump = (epump_t *)veps;
    iodev_t  * pdev = (iodev_t *)vpdev;
    iodev_t  * obj = NULL;
 
    if (!epump || !pdev) return -1;
 
    if (pdev->fd == INVALID_SOCKET) return -2;

    if (epcore_iodev_find(epump->epcore, pdev->id) != pdev) {
        tolog(1, "DevAdd: [%lu %d %s %d %d] Dev=%d/%d DPool=%d/%d Timer=%d/%d TPool=%d/%d ePump=%lu\n",
              pdev->id, pdev->fd, pdev->remote_ip, pdev->fdtype, pdev->bindtype,
              rbtree_num(epump->device_tree), ht_num(epump->epcore->device_table),
              mpool_allocated(epump->epcore->device_pool), mpool_consumed(epump->epcore->device_pool),
              rbtree_num(epump->timer_tree), ht_num(epump->epcore->timer_table),
              mpool_allocated(epump->epcore->timer_pool), mpool_consumed(epump->epcore->timer_pool),
              epump->threadid);
        return -3;
    }
 
    EnterCriticalSection(&epump->devicetreeCS);
 
    obj = rbtree_get(epump->device_tree, (void *)(long)pdev->fd);
    if (obj != pdev) {
 
        if (obj != NULL) {
            if (rbtree_delete(epump->device_tree, (void *)(long)pdev->fd) != NULL) {
                tolog(1, "Panic: multi-dev on fd=%d when adddev[%lu %s:%d type:%d bind:%d] "
                         "dupdev[%lu %s:%d %d %d], epump[%lu] epmfd=%d, epcofd=%d\n",
                      pdev->fd, pdev->id, pdev->remote_ip, pdev->remote_port, pdev->fdtype,
                      pdev->bindtype, obj->id, obj->remote_ip, obj->remote_port, obj->fdtype, obj->bindtype,
                      epump->threadid, rbtree_num(epump->device_tree), ht_num(epump->epcore->device_table));
            }
        }
 
        rbtree_insert(epump->device_tree, (void *)(long)pdev->fd, pdev, NULL);
    }
 
    LeaveCriticalSection(&epump->devicetreeCS);
 
    if (obj && obj != pdev) {
        obj->fd = INVALID_SOCKET;
        iodev_close(obj);
    }

    return 0;
#endif
}
 
void * epump_iodev_del (void * veps, SOCKET fd)
{
    epump_t * epump = (epump_t *) veps;
    iodev_t * pdev = NULL;
 
    if (!epump) return NULL;
 
    EnterCriticalSection(&epump->devicetreeCS);
 
    pdev = rbtree_delete(epump->device_tree, (void *)(long)fd);
    if (!pdev) {
        LeaveCriticalSection(&epump->devicetreeCS);
        return NULL;
    }
 
    LeaveCriticalSection(&epump->devicetreeCS);
 
    return pdev;
}
 
 
void * epump_iodev_find (void * vepump, SOCKET fd)
{
    epump_t  * epump = (epump_t *) vepump;
    iodev_t  * pdev = NULL;
 
    if (!epump) return NULL;
 
    EnterCriticalSection(&epump->devicetreeCS);
 
    pdev = rbtree_get(epump->device_tree, (void *)(long)fd);
 
    LeaveCriticalSection(&epump->devicetreeCS);
 
    return pdev;
}
 
int epump_iodev_tcpnum (void * vepump)
{
    epump_t  * epump = (epump_t *) vepump;
    iodev_t  * pdev = NULL;
    int        i, num, retval = 0;
    rbtnode_t * rbtn = NULL;
 
    if (!epump) return 0;
 
    EnterCriticalSection(&epump->devicetreeCS);
 
    rbtn = rbtree_min_node(epump->device_tree);
    num = rbtree_num(epump->device_tree);
 
    for (i = 0; i < num && rbtn; i++) {
        pdev = RBTObj(rbtn);
        rbtn = rbtnode_next(rbtn);
 
        if (!pdev) continue;
        if (pdev->fdtype == FDT_CONNECTED || pdev->fdtype == FDT_ACCEPTED)
            retval++;
    }
 
    LeaveCriticalSection(&epump->devicetreeCS);
 
    return retval;
}
 
void epump_iodev_print (void * vepump, int printtype)
{
    epump_t   * epump = (epump_t *) vepump;
    iodev_t   * pdev = NULL;
    int         i, num, iter = 0;
    rbtnode_t * rbtn = NULL;
    char        buf[32768];
 
    if (!epump) return;
 
    EnterCriticalSection(&epump->devicetreeCS);
 
    rbtn = rbtree_min_node(epump->device_tree);
    num = rbtree_num(epump->device_tree);
 
    sprintf(buf, " ePump:%lu FDnum=%d:", epump->threadid, num);
    iter = strlen(buf);
 
    for (i = 0; i < num && rbtn; i++) {
        pdev = RBTObj(rbtn);
        rbtn = rbtnode_next(rbtn);
 
        if (!pdev) continue;
 
        sprintf(buf+iter, "%s %d/%lu", (i%8)==0?"\n":"", pdev->fd, pdev->id);
        iter = strlen(buf);
        if (iter > sizeof(buf) - 16) break;
    }
    if (printtype == 0) printf("%s\n", buf);
    else if (printtype == 1) tolog(1, "%s\n", buf);
 
    LeaveCriticalSection(&epump->devicetreeCS);
}
 
SOCKET epump_iodev_maxfd (void * vepump)
{
    epump_t * epump = (epump_t *) vepump;
    iodev_t * pdev = NULL;
 
    if (!epump) return 0;
 
    EnterCriticalSection(&epump->devicetreeCS);
    pdev = rbtree_max(epump->device_tree);
    LeaveCriticalSection(&epump->devicetreeCS);
 
    if (pdev)
        return pdev->fd + 1;
 
    return 0;
}
 
int epump_device_scan (void * vepump)
{
    epump_t   * epump = (epump_t *) vepump;
    iodev_t   * pdev = NULL;
    int         i, num;
    fd_set      rFds, wFds;
    int         nFds = 0;
    uint8       setpoll = 0;
    rbtnode_t * rbt = NULL;
    struct timeval  timeout, *tout;
 
    EnterCriticalSection(&epump->devicetreeCS);
 
#if defined(HAVE_SELECT)
    FD_ZERO(&epump->readFds);
    FD_ZERO(&epump->writeFds);
#endif
 
    num = rbtree_num(epump->device_tree);
 
    rbt = rbtree_min_node(epump->device_tree);
 
    for (i = 0; i < num && rbt; i++) {
        pdev = RBTObj(rbt);
        rbt = rbtnode_next(rbt);
        if (!pdev) {
            rbtree_delete_node(epump->device_tree, rbt);
            continue;
        }
 
        FD_ZERO(&rFds);
        FD_ZERO(&wFds);
 
        setpoll = 0;
 
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
                /* clean up */
                FD_CLR (pdev->fd, &rFds);
                pdev->rwflag &= ~RWF_READ;
 
                PushInvalidDevEvent(epump, pdev);
            } else {
                setpoll = 1;
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
                /* clean up */
                FD_CLR (pdev->fd, &wFds);
                pdev->rwflag &= ~RWF_WRITE;
 
                PushInvalidDevEvent(epump, pdev);
            } else {
                setpoll = 1;
            }
        }
 
        if (setpoll)
            (*epump->setpoll)(epump, pdev);
 
    }
 
    LeaveCriticalSection(&epump->devicetreeCS);
 
    return 0;
}
 
 
int epump_main_proc (void * veps)
{
    epump_t   * epump = (epump_t *)veps;
    epcore_t  * pcore = NULL;
    int         ret = 0;
    int         evnum = 0;
    btime_t     diff, * pdiff = NULL;
 
    if (!epump) return -1;
 
    pcore = (epcore_t *)epump->epcore;
    if (!pcore) return -2;
 
    epump->threadid = get_threadid();
    epump_thread_add(pcore, epump);
 
    /* wake up the epoll_wait while waiting in block for the fd-set ready */
    epump_wakeup_init(epump);
 
    /* now append the global fd's in epcore to current epoll-fd monitoring list */
    epcore_global_iodev_getmon(pcore, epump);
    epcore_global_iotimer_getmon(pcore, epump);
 
    /* create a listen socket for every mlisten instances in current epump */
    epcore_mlisten_create(pcore, epump);
 
    while (pcore->quit == 0 && epump->quit == 0) {

        if (lt_num(epump->ioevent_list) > 0)
            ioevent_handle(epump);
 
        do {
            ret = iotimer_check_timeout(epump, &diff, &evnum);
 
            if (evnum > 0)
                ioevent_handle(epump);
        } while (evnum > 0);
 
        if (pcore->quit || epump->quit)
            break;
 
        /* waiting for the readiness notification from the monitored fd-list */
        if (ret < 0) pdiff = NULL;
        else pdiff = &diff;

        epump->epumpsleep = 1;
        (*epump->fddispatch)(epump, pdiff);
        epump->epumpsleep = 0;
    }
 
    epump->quit = 1;
 
    return 0;
}
 
 
 
#if defined(_WIN32) || defined(_WIN64)
unsigned WINAPI epump_main_thread (void * arg)
{
#endif
#ifdef UNIX
void * epump_main_thread (void * arg)
{
    pthread_detach(pthread_self());
#endif
 
    epump_main_proc(arg);
 
#ifdef UNIX
    return NULL;
#endif
#if defined(_WIN32) || defined(_WIN64)
    return 0;
#endif
}
 
 
int epump_main_start (void * vpcore, int forkone)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    epump_t   * epump = NULL;
#if defined(_WIN32) || defined(_WIN64)
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
 
    if (!forkone) {
        epump_main_proc(epump);
        return 0;
    }
 
#if defined(_WIN32) || defined(_WIN64)
    epump->epumphandle = (HANDLE)_beginthreadex(
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
#endif
 
    return 0;
}
 
void epump_main_stop (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return;
 
    epump->quit = 1;
 
    epump_wakeup_send(epump);
}

