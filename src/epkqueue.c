/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifdef HAVE_KQUEUE

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

#include <sys/resource.h>


int epump_kqueue_init (epump_t * epump, int maxfd)
{
    struct rlimit rl;

    if (!epump) return -1;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        epump->kqueue_size = rl.rlim_cur;
    } else {
        epump->kqueue_size = maxfd;
    }

    epump->kqueue_events = kzalloc(epump->kqueue_size * sizeof(struct kevent));
    if (!epump->kqueue_events) {
        return -200;
    }
    memset(epump->kqueue_events, 0, epump->kqueue_size * sizeof(struct kevent));
    
    epump->kqueue_fd = kqueue();
    if (epump->kqueue_fd < 0) {
        kfree(epump->kqueue_events);
        epump->kqueue_events = NULL;
        return -300;
    }
    
    return 0; 
}

int epump_kqueue_clean (epump_t * epump)
{
    if (!epump) return -1;

    /* clean the kqueue facilities */
    if (epump->kqueue_fd >= 0) {
        close(epump->kqueue_fd);
        epump->kqueue_fd = -1;
    }
    if (epump->kqueue_events) {
        kfree(epump->kqueue_events);
        epump->kqueue_events = NULL;
    }
    return 0; 
}


int epump_kqueue_setpoll (void * vepump, void * vpdev)
{
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = (iodev_t *)vpdev;
    int         ret = 0;
    int         n = 0;
    struct      kevent ev[2];

    if (!epump || !pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) return -2;

    memset(ev, 0, sizeof(ev));
    
    if (pdev->rwflag & RWF_READ) {
        EV_SET(&ev[n++], pdev->fd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, (void*)pdev);
    } else {
        EV_SET(&ev[n++], pdev->fd, EVFILT_READ, EV_DELETE, 0, 0, (void*)pdev);
    }
 
    if (pdev->rwflag & RWF_WRITE) {
        EV_SET(&ev[n++], pdev->fd, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, (void*)pdev);
    } else {
        EV_SET(&ev[n++], pdev->fd, EVFILT_WRITE, EV_DELETE, 0, 0, (void*)pdev);
    }

    ret = kevent(epump->kqueue_fd, ev, n, NULL, 0, NULL);
    if (ret <= 0) return -2;
 
    return 0;
}

int epump_kqueue_clearpoll (void * vepump, void * vpdev)
{
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = (iodev_t *)vpdev;
    struct      kevent ev[2];
    int         ret = 0;
    
    if (!epump || !pdev) return -1;

    if (pdev->fd == INVALID_SOCKET) return -2;
    
    memset(ev, 0 ,sizeof(ev));
    
    EV_SET(ev, pdev->fd, EVFILT_READ, EV_DELETE, 0, 0, (void*)pdev);
    EV_SET(&ev[1], pdev->fd, EVFILT_WRITE, EV_DELETE, 0, 0, (void*)pdev);

    ret = kevent(epump->kqueue_fd, ev, 2, NULL, 0, NULL);
    if (ret <= 0) return -3;
    
    return 0;
}


int epump_kqueue_dispatch (void * veps, btime_t * delay)
{
    epump_t  * epump = (epump_t *)veps;
    epcore_t * pcore = NULL;
    int        nfds= -1, events, i;
    iodev_t  * pdev = NULL;
    int        addrlen;
    struct timespec * waitout, timeout;
    struct sockaddr  sock;

    if (!epump) return -1;

    pcore = epump->epcore;
    if (!pcore) return -2;

    if (delay == NULL) {
        waitout= NULL;
    } else {
        timeout.tv_sec = delay->s;
        timeout.tv_nsec = delay->ms * 1000 * 1000;
        waitout = &timeout;
    }

    nfds = kevent(epump->kqueue_fd, NULL, 0, epump->kqueue_events, epump->kqueue_size, waitout);
    if (nfds < 0) {
        if (errno != EINTR) return -1;
        return 0;
    }

    if (pcore->quit) return 0;
 
    for (i = 0; i < nfds; i++) {
        
        events = epump->kqueue_events[i].filter;
        pdev = epump->kqueue_events[i].udata;
        if (!pdev) continue;
        
        if (pdev->fd == INVALID_SOCKET) {
            iodev_del_notify(pdev, RWF_READ);
            PushInvalidDevEvent(epump, pdev);
            continue;
        }
 
        if (events == EVFILT_READ) {
            if (pdev->fdtype == FDT_LISTEN) {
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
        }

        if (events == EVFILT_WRITE) {
            iodev_del_notify(pdev, RWF_WRITE);
 
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

    return 0;
}

#endif

