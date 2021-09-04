/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifdef HAVE_SELECT

#include "btype.h"
#include "tsock.h"
#include "dynarr.h"
#include "mthread.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epwakeup.h"

#ifdef UNIX
#include <sys/select.h>
#endif

int epump_select_init (epump_t * epump)
{
    if (!epump) return -1;

    InitializeCriticalSection(&epump->fdsetCS);
    FD_ZERO(&epump->readFds);
    FD_ZERO(&epump->writeFds);
    FD_ZERO(&epump->exceptionFds);

    return 0; 
}

int epump_select_clean (epump_t * epump)
{
    if (!epump) return -1;

    DeleteCriticalSection(&epump->fdsetCS);
    FD_ZERO(&epump->readFds);
    FD_ZERO(&epump->writeFds);
    FD_ZERO(&epump->exceptionFds);

    return 0; 
}


int epump_select_setpoll (void * vepump, void * vpdev)
{
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = (iodev_t *)vpdev;
    int         wakeepump = 0;

    if (!epump || !pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) return -2;

    EnterCriticalSection(&epump->fdsetCS);
 
    if (pdev->rwflag & RWF_READ) {
        if (!FD_ISSET(pdev->fd, &epump->readFds)) {
            FD_SET(pdev->fd, &epump->readFds);
            wakeepump = 1;
        }
    } else {
        if (FD_ISSET(pdev->fd, &epump->readFds)) {
            FD_CLR(pdev->fd, &epump->readFds);
            wakeepump = 1;
        }
    }
 
    if (pdev->rwflag & RWF_WRITE) {
        if (!FD_ISSET(pdev->fd, &epump->writeFds)) {
            FD_SET(pdev->fd, &epump->writeFds);
            wakeepump = 1;
        }
    } else {
        if (FD_ISSET(pdev->fd, &epump->writeFds)) {
            FD_CLR(pdev->fd, &epump->writeFds);
            wakeepump = 1;
        }
    }
 
    LeaveCriticalSection(&epump->fdsetCS);
 
    if (wakeepump)
        epump_wakeup_send(epump);

    return 0;
}

int epump_select_clearpoll (void * vepump, void * vpdev)
{
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = (iodev_t *)vpdev;

    if (!epump || !pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) return -2;

    EnterCriticalSection(&epump->fdsetCS);

    if (FD_ISSET(pdev->fd, &epump->readFds)) {
        FD_CLR(pdev->fd, &epump->readFds);
    }

    if (FD_ISSET(pdev->fd, &epump->writeFds)) {
        FD_CLR(pdev->fd, &epump->writeFds);
    }

    LeaveCriticalSection(&epump->fdsetCS);

    return 0;
}


int epump_select_dispatch (void * veps, btime_t * delay)
{
    epump_t  * epump = (epump_t *)veps;
    epcore_t * pcore = NULL;
    fd_set     rFds, wFds;
    int        maxfd, nfds, num, i;
    iodev_t  * pdev = NULL;
    int        addrlen;
    rbtnode_t * rbt = NULL;
    struct timeval * waitout, timeout;
    ep_sockaddr_t    sock;

    if (!epump) return -1;

    pcore = epump->epcore;
    if (!pcore) return -2;

    if (delay == NULL) {
        waitout= NULL;
    } else {
        timeout.tv_sec = delay->s;
        timeout.tv_usec = delay->ms * 1000;
        waitout = &timeout;
    }

    /* Generate local copies of fd sets */
    rFds = epump->readFds;
    wFds = epump->writeFds;

    maxfd = epump_iodev_maxfd(epump);
    if (maxfd <= 0) maxfd = 1024;

    /* nfds is sum of ready read fd's and write fd's */
#ifdef UNIX
    nfds = select (maxfd, &rFds, &wFds, NULL, waitout);
#else
#if defined(_WIN32) || defined(_WIN64)
    nfds = select (0, &rFds, &wFds, NULL, waitout);
#endif
#endif

    if (nfds < 0) {
        if (errno != EINTR) return -1;
        return 0;
    }

    EnterCriticalSection(&epump->devicetreeCS);

    num = rbtree_num(epump->device_tree);
    rbt = rbtree_max_node(epump->device_tree);
 
    for (i = 0; i < num && rbt; i++) {
        pdev = RBTObj(rbt);
        rbt = rbtnode_prev(rbt);
        if (!pdev) {
            rbtree_delete_node(epump->device_tree, rbt);
            continue;
        }

        if (pdev->fd == INVALID_SOCKET) {
            iodev_del_notify(pdev, RWF_READ);
            PushInvalidDevEvent(epump, pdev);
            continue;
        }
 
        if (pdev->rwflag & RWF_READ && FD_ISSET(pdev->fd, &rFds)) {
            nfds--;
            if (pdev->fdtype == FDT_LISTEN) {
                epump_select_clearpoll(epump, pdev);

                PushConnAcceptEvent(epump, pdev);

#ifdef HAVE_EVENTFD
            } else if (pdev == epump->wakeupdev || pdev->fd == epump->wakeupfd) {
                epump_wakeup_recv(epump);
#endif
            } else if (pdev == pcore->wakeupdev || pdev->fd == pcore->wakeupfd) {
                epcore_wakeup_recv(pcore);

            } else {
                epump_select_clearpoll(epump, pdev);

                PushReadableEvent(epump, pdev);
            }
        }

        if (pdev->rwflag & RWF_WRITE && FD_ISSET(pdev->fd, &wFds)) {
            iodev_del_notify(pdev, RWF_WRITE);
 
            nfds--;
            if (pdev->iostate == IOS_CONNECTING && pdev->fdtype == FDT_CONNECTED) {
                int sockerrlen = sizeof(int);
                int sockerr = 0;
                int retval = getsockopt(pdev->fd, SOL_SOCKET, SO_ERROR,
                                        (char *)&sockerr, (socklen_t *)&sockerrlen);
                if (retval < 0 || sockerr != 0) {
                    PushConnfailEvent(epump, pdev);
                } else {
                    addrlen = sizeof(sock);
                    if (getsockname(pdev->fd, (struct sockaddr *)&sock,
                                    (socklen_t *)&addrlen) == 0)
                    {
                        sock_addr_ntop(&sock, pdev->local_ip);
                        pdev->local_port = sock_addr_port(&sock);
                    }
 
                    addrlen = sizeof(sock); 
                    if (getpeername(pdev->fd, (struct sockaddr *)&sock,  
                                   (socklen_t *)&addrlen) == 0) { 
                        sock_addr_ntop(&sock, pdev->remote_ip);
                        pdev->remote_port = sock_addr_port(&sock);
                    }

                    PushConnectedEvent(epump, pdev);
                }
            } else {
                PushWritableEvent(epump, pdev);
            }
        }
    }

    LeaveCriticalSection(&epump->devicetreeCS);

    return 0;
}

#endif

