/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifdef HAVE_EPOLL

#include "btype.h"
#include "memory.h"
#include "tsock.h"
#include "btime.h"
#include "mthread.h"
#include "hashtab.h"
#include "bpool.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epwakeup.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/epoll.h>


int epump_epoll_init (epump_t * epump, int maxfd)
{
    struct rlimit rl;

    if (!epump) return -1;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        epump->epoll_size = rl.rlim_cur;
    } else {
        epump->epoll_size = maxfd;
    }
 
    epump->epoll_fd = epoll_create(epump->epoll_size);
    if (epump->epoll_fd < 0) {
        return -100;
    }

    epump->epoll_events = kzalloc(epump->epoll_size * sizeof(struct epoll_event));
    if (!epump->epoll_events) {
        epump->epoll_fd = -1;
        close(epump->epoll_fd);
        return -200;
    }

    return 0;
}

int epump_epoll_clean (epump_t * epump)
{
    if (!epump) return -1;

    /* clean the epoll facilities */
    if (epump->epoll_fd >= 0) {
        close(epump->epoll_fd);
        epump->epoll_fd = -1;
    }
    if (epump->epoll_events) {
        kfree(epump->epoll_events);
        epump->epoll_events = NULL;
    }

    return 0;
}

int epump_epoll_setpoll (void * vepump, void * vpdev)
{
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = (iodev_t *)vpdev;
    uint32      curev = 0;
    int         op = 0;  /* 1-add 2-mod 3-del */
    int         ret = 0;
    struct epoll_event ev = { 0, {0} };

    if (!epump || !pdev) return -1;

    if (pdev->fd < 0) return -2;

    memset(&ev, 0, sizeof(ev));
 
    ev.data.ptr = pdev;
 
    if (pdev->rwflag & RWF_READ) {
        if (pdev->fdtype == FDT_UDPSRV ||
            pdev->fdtype == FDT_UDPCLI ||
            pdev->fdtype == FDT_RAWSOCK)
            ev.events |= EPOLLIN | EPOLLET;
        else
            ev.events |= EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
        curev |= EPOLLIN;

        //after test, OneShot of ListenDev does not solve thundering herd problem
        /*if (pdev->fdtype == FDT_LISTEN)
            ev.events |= EPOLLONESHOT;*/
    }
 
    if (pdev->rwflag & RWF_WRITE) {
        ev.events |= EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP;
        curev |= EPOLLOUT;
    }
 
    if (ev.events != 0)
        op = EPOLL_CTL_MOD;
    else 
        op = EPOLL_CTL_DEL;

    ret = epoll_ctl(epump->epoll_fd, op, pdev->fd, &ev);
    if (ret >= 0) {
        return 0;
    }

    switch (op) {
    case EPOLL_CTL_ADD:
        if (errno == EEXIST) {
            ret = epoll_ctl(epump->epoll_fd, EPOLL_CTL_MOD, pdev->fd, &ev);
        }
        break;
    case EPOLL_CTL_MOD:
        if (errno == ENOENT) {
            ret = epoll_ctl(epump->epoll_fd, EPOLL_CTL_ADD, pdev->fd, &ev);
        }
        break;
    case EPOLL_CTL_DEL:
        if (errno == ENOENT || errno == EBADF || errno == EPERM) {
            return 0;
        }
        break;
    }

    if (ret < 0) return -1;

    return 0;
}


int epump_epoll_clearpoll (void * vepump, void * vpdev)
{
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = (iodev_t *)vpdev;
    struct epoll_event ev = { 0, {0} };
    int         ret = 0;

    if (!epump || !pdev) return -1;

    if (pdev->fd < 0) return -2;

    memset(&ev, 0, sizeof(ev));

    ret = epoll_ctl(epump->epoll_fd, EPOLL_CTL_DEL, pdev->fd, &ev);

    return ret;
}



int epump_epoll_dispatch (void * veps, btime_t * delay)
{
    epump_t  * epump = (epump_t *)veps;
    epcore_t * pcore = NULL;
    int        waitms = 0;
    int        i, nfds; 
    uint32     whatup;
    iodev_t  * pdev = NULL;
    int        len;
    int        ret = 0;
    int        sockerr = 0;
    ep_sockaddr_t sock;

    if (!epump) return -1;

    pcore = epump->epcore;
    if (!pcore) return -2;

    if (delay == NULL) {
        waitms = -1;
    } else {
        waitms = delay->s * 1000 + delay->ms;
        if (waitms > MAX_EPOLL_TIMEOUT_MSEC) waitms = MAX_EPOLL_TIMEOUT_MSEC;
    }

    /* nfds is sum of ready read fd's and write fd's */
    nfds = epoll_wait(epump->epoll_fd, epump->epoll_events, epump->epoll_size, waitms);
    if (nfds < 0) {
        if (errno != EINTR) return -1;
        return 0;
    }

    if (pcore->quit) return 0;

    for (i = 0; i < nfds; i++) {
        whatup = epump->epoll_events[i].events;
        pdev = epump->epoll_events[i].data.ptr;
        if (!pdev) continue;
 
        if (whatup & EPOLLIN) {
            if (pdev->fdtype == FDT_LISTEN || pdev->fdtype == FDT_USOCK_LISTEN) {
                PushConnAcceptEvent(epump, pdev);

#ifdef HAVE_EVENTFD
            } else if (pdev == epump->wakeupdev || pdev->fd == epump->wakeupfd) {
                epump_wakeup_recv(epump);
#endif

            } else if (pdev == pcore->wakeupdev || pdev->fd == pcore->wakeupfd) {
                epcore_wakeup_recv(pcore);

            } else {
                PushReadableEvent(epump, pdev);
            }

        } else if (whatup & EPOLLOUT) {
            iodev_del_notify(pdev, RWF_WRITE);
 
            if (pdev->iostate == IOS_CONNECTING && pdev->fdtype == FDT_CONNECTED) {
                len = sizeof(int);
                sockerr = 0;
                ret = getsockopt(pdev->fd, SOL_SOCKET, SO_ERROR,
                                        (char *)&sockerr, (socklen_t *)&len);

                if (ret < 0 || sockerr != 0) {
                    PushConnfailEvent(epump, pdev);

                } else {
                    len = sizeof(sock);
                    if (getsockname(pdev->fd, (struct sockaddr *)&sock,
                                    (socklen_t *)&len) == 0) {
                        sock_addr_ntop(&sock, pdev->local_ip);
                        pdev->local_port = sock_addr_port(&sock);
                    }

                    len = sizeof(sock);
                    if (getpeername(pdev->fd, (struct sockaddr *)&sock,
                                   (socklen_t *)&len) == 0) {
                        sock_addr_ntop(&sock, pdev->remote_ip);
                        pdev->remote_port = sock_addr_port(&sock);
                    }

 
                    PushConnectedEvent(epump, pdev);
                }

            } else {
                PushWritableEvent(epump, pdev);
            }

        } else if (whatup & (EPOLLHUP | EPOLLERR)) {
            PushInvalidDevEvent(epump, pdev);

        } else {
            PushInvalidDevEvent(epump, pdev);
        }
    } /* end for */

    return 0;
}

#endif

