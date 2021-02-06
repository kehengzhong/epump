/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved.
 */

#ifdef UNIX

#include "btype.h"
#include "tsock.h"
#include "usock.h"

#include "epcore.h"
#include "iodev.h"


void * epusock_connect (void * vpcore, char * sockname, void * para,
                        int * retval, IOHandler * ioh, void * iohpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    int        succ = 0;
 
    if (retval) *retval = -1;
    if (!pcore || !sockname) return NULL;
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -20;
        return NULL;
    }
 
    pdev->fdtype = FDT_USOCK_CONNECTED;
 
    pdev->callback = ioh;
    pdev->para = para;
    pdev->cbpara = iohpara;

    pdev->fd = usock_nb_connect(sockname, &succ);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -30;
        return NULL;
    }
 
    /* indicates the current worker thread will handle the upcoming read/write event */
    if (pcore->dispmode == 1)
        pdev->threadid = get_threadid();
 
    if (succ > 0) { /* connect successfully */
        pdev->iostate = IOS_READWRITE;
        if (retval) *retval = 0;
 
        iodev_rwflag_set(pdev, RWF_READ);
 
    } else {
        pdev->iostate = IOS_CONNECTING;
        if (retval) *retval = -100;
 
        iodev_rwflag_set(pdev, RWF_READ | RWF_WRITE);
    }
 
    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, BIND_CURRENT_EPUMP, NULL);
 
    return pdev;
}
 
 
void * epusock_listen (void * vpcore, char * sockname, void * para, int * retval,
                       IOHandler * cb, void * cbpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
 
    if (retval) *retval = -1;
    if (!pcore || !sockname) return NULL;
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -100;
        return NULL;
    }
 
    pdev->fd = usock_create(sockname);
    if (pdev->fd < 0) {
        iodev_close(pdev);
        if (retval) *retval = -200;
        return NULL;
    }
 
    sock_nonblock_set(pdev->fd, 1);
 
    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    pdev->iostate = IOS_ACCEPTING;
    pdev->fdtype = FDT_USOCK_LISTEN;
 
    iodev_rwflag_set(pdev, RWF_READ);
 
    /* all epump threads will be monitoring Listen FD */
    iodev_bind_epump(pdev, BIND_ALL_EPUMP, NULL);

    if (retval) *retval = 0;
    return pdev;
}
 
 
void * epusock_accept (void * vpcore, void * vld, void * para, int * retval,
                       IOHandler * cb, void * cbpara, int bindtype)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * listendev = (iodev_t *)vld;
    iodev_t  * pdev = NULL;
    int        clifd;
 
    if (retval) *retval = -1;
    if (!pcore || !listendev) return NULL;
 
    EnterCriticalSection(&listendev->fdCS);
    clifd = usock_accept(listendev->fd);
    LeaveCriticalSection(&listendev->fdCS);

    if (clifd < 0) {
        if (retval) *retval = -100;
        return NULL;
    }
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -200;
        close(clifd);
        return NULL;
    }
 
    /* indicates the current worker thread will handle the upcoming read/write event */
    if (pcore->dispmode == 1)
        pdev->threadid = get_threadid();

    pdev->fd = clifd;
 
    sock_nonblock_set(pdev->fd, 1);
 
    pdev->fdtype = FDT_USOCK_ACCEPTED;
    pdev->iostate = IOS_READWRITE;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;

    iodev_rwflag_set(pdev, RWF_READ);

    if (retval) *retval = 0;

    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, bindtype, NULL);
 
    return pdev;
}

#endif

