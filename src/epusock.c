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

#ifdef UNIX

#include "btype.h"
#include "tsock.h"
#include "usock.h"

#include "epcore.h"
#include "iodev.h"


void * epusock_connect (void * vpcore, char * sockname, void * para, IOHandler * ioh,
                        void * iohpara, ulong threadid, int * retval)
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
 
    /* indicates which worker thread will handle the upcoming read/write event */
    if (threadid > 0)
        pdev->threadid = threadid;
    else
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
    iodev_bind_epump(pdev, BIND_GIVEN_EPUMP, pdev->threadid, 0);
 
    return pdev;
}
 
 
void * epusock_listen (void * vpcore, char * sockname, void * para,
                       IOHandler * cb, void * cbpara, int * retval)
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
    iodev_bind_epump(pdev, BIND_ALL_EPUMP, 0, 0);

    if (retval) *retval = 0;
    return pdev;
}
 
 
void * epusock_accept (void * vpcore, void * vld, void * para, IOHandler * cb,
                       void * cbpara, int bindtype, ulong threadid, int * retval)
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
 
    /* indicates which worker thread will handle the upcoming read/write event */
    if (threadid > 0)
        pdev->threadid = threadid;
    else
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
    iodev_bind_epump(pdev, bindtype, pdev->threadid, 0);
 
    return pdev;
}

#endif

