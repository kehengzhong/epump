/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */
 
#include "btype.h"
#include "bpool.h"
#include "tsock.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "epwakeup.h"

#ifdef HAVE_IOCP
#include "epiocp.h"
#endif


#ifdef HAVE_EVENTFD

#ifdef UNIX
#include <sys/eventfd.h>
#include <unistd.h>
#include <sys/epoll.h>
#endif


int epcore_wakeup_init (void * vpcore)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;

    if (!pcore) return -1;

    pcore->wakeupfd = eventfd(0, 0);
    sock_nonblock_set(pcore->wakeupfd, 1);

    pcore->wakeupdev = pdev = (iodev_t *)bpool_fetch(pcore->device_pool);
    if (pdev == NULL) {
        pdev = iodev_alloc();
        if (pdev == NULL) return -100;
    }
    pdev->fd = pcore->wakeupfd;
    pdev->fdtype = FDT_FILEDEV;
    pdev->iostate = IOS_READWRITE;
    pdev->rwflag = RWF_READ;

    return 0;
}

int epcore_wakeup_clean (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
 
    if (!pcore) return -1;
 
    close(pcore->wakeupfd);
    pcore->wakeupfd = -1;

    iodev_free(pcore->wakeupdev);
    pcore->wakeupdev = NULL;

    return 0;
}

int epcore_wakeup_send (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    uint64     val = 1;
 
    if (!pcore) return -1;

    write(pcore->wakeupfd, &val, sizeof(val));

    return 0;
}

int epcore_wakeup_recv (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    uint64     val = 0;
 
    if (!pcore) return -1;
 
    read(pcore->wakeupfd, &val, sizeof(val));

    return 0;
}

int epcore_wakeup_getmon (void * vpcore, void * veps)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    epump_t  * epump = (epump_t *)veps;

#ifdef HAVE_EPOLL

    struct epoll_event ev = { 0, {0} };

    if (!pcore) return -1;
    if (!epump) return -2;

    epump_iodev_add(epump, pcore->wakeupdev);

    ev.data.ptr = pcore->wakeupdev;
    ev.events = EPOLLIN;
    if (epoll_ctl(epump->epoll_fd, EPOLL_CTL_ADD, pcore->wakeupfd, &ev) < 0) {
        if (errno == EEXIST)
            epoll_ctl(epump->epoll_fd, EPOLL_CTL_MOD, pcore->wakeupfd, &ev);
    }

    return 0;

#elif defined(HAVE_KQUEUE)

    struct      kevent ev[1];

    if (!pcore) return -1;
    if (!epump) return -2;

    epump_iodev_add(epump, pcore->wakeupdev);
    memset(ev, 0 ,sizeof(ev));

    EV_SET(ev, pcore->wakeupfd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, (void*)pcore->wakeupdev);
    kevent(epump->kqueue_fd, ev, 1, NULL, 0, NULL);

    return 0;

#elif defined(HAVE_SELECT)

    if (!pcore) return -1;
    if (!epump) return -2;

    epump_iodev_add(epump, pcore->wakeupdev);

    if (!FD_ISSET(pcore->wakeupfd, &epump->readFds)) {
        FD_SET(pcore->wakeupfd, &epump->readFds);
    }

    return 0;
#endif
}

int epump_wakeup_init (void * vepump)
{
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = NULL;
#ifdef HAVE_EPOLL
    struct epoll_event ev = { 0, {0} };
#elif defined(HAVE_KQUEUE)
    struct kevent ev[1];
#endif
 
    if (!epump) return -1;
 
    epump->wakeupfd = eventfd(0, 0);
    sock_nonblock_set(epump->wakeupfd, 1);
 
    epump->wakeupdev = pdev = iodev_alloc();
    if (pdev == NULL) {
        return -100;
    }
    pdev->fd = epump->wakeupfd;
    pdev->fdtype = FDT_FILEDEV;
    pdev->iostate = IOS_READWRITE;
    pdev->rwflag = RWF_READ;
 
    epump_iodev_add(epump, epump->wakeupdev);
 
#ifdef HAVE_EPOLL

    ev.data.ptr = epump->wakeupdev;
    ev.events = EPOLLIN;
    if (epoll_ctl(epump->epoll_fd, EPOLL_CTL_ADD, epump->wakeupfd, &ev) < 0) {
        if (errno == EEXIST)
            epoll_ctl(epump->epoll_fd, EPOLL_CTL_MOD, epump->wakeupfd, &ev);
    }

#elif defined(HAVE_KQUEUE)

    memset(ev, 0 ,sizeof(ev));

    EV_SET(ev, epump->wakeupfd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, (void*)epump->wakeupdev);
    kevent(epump->kqueue_fd, ev, 1, NULL, 0, NULL);

#elif defined(HAVE_SELECT)

    if (!FD_ISSET(epump->wakeupfd, &epump->readFds)) {
        FD_SET(epump->wakeupfd, &epump->readFds);
    }

#endif

    return 0;
}
 
int epump_wakeup_clean (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
#ifdef HAVE_EPOLL
    struct epoll_event ev = { 0, {0} };
#elif defined(HAVE_KQUEUE)
    struct kevent ev[1];
#endif

    if (!epump) return -1;

    if (epump_iodev_del(epump, epump->wakeupfd) != NULL) {
#ifdef HAVE_EPOLL
        epoll_ctl(epump->epoll_fd, EPOLL_CTL_DEL, epump->wakeupfd, &ev);

#elif defined(HAVE_KQUEUE)
        memset(ev, 0 ,sizeof(ev));
        EV_SET(ev, epump->wakeupfd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(epump->kqueue_fd, ev, 1, NULL, 0, NULL);

#elif defined(HAVE_SELECT)
        if (FD_ISSET(epump->wakeupfd, &epump->readFds)) {
            FD_CLR(epump->wakeupfd, &epump->readFds);
        }
#endif
    }

    close(epump->wakeupfd);
    epump->wakeupfd = -1;
 
    iodev_free(epump->wakeupdev);
    epump->wakeupdev = NULL;
 
    return 0;
}
 
int epump_wakeup_send (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
    uint64     val = 1;
 
    if (!epump) return -1;
 
    write(epump->wakeupfd, &val, sizeof(val));
 
    return 0;
}
 
int epump_wakeup_recv (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
    uint64     val = 0;
 
    if (!epump) return -1;
 
    read(epump->wakeupfd, &val, sizeof(val));
 
    return 0;
}
 
#elif defined(HAVE_IOCP)

int epcore_wakeup_init (void * vpcore)
{
    return 0;
}

int epcore_wakeup_clean (void * vpcore)
{
    return 0;
}

int epcore_wakeup_send (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    int        i;

    if (!pcore) return -1;

    for (i = 0; i < arr_num(pcore->epump_list); i++) {
        PostQueuedCompletionStatus(pcore->iocp_port, 0, (ULONG_PTR)-1, NULL);
    }

    return 0;
}

int epcore_wakeup_recv (void * vpcore)
{
    return 0;
}

int epcore_wakeup_getmon (void * vpcore, void * veps)
{
    return 0;
}

int epump_wakeup_init (void * vepump)
{
    return 0;
}
 
int epump_wakeup_clean (void * vepump)
{
    return 0;
}
 
int epump_wakeup_send (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_send(epump->epcore);
}
 
int epump_wakeup_recv (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_recv(epump->epcore);
}

#else

int epcore_wakeup_init (void * vpcore)
{
    epcore_t    * pcore = (epcore_t *)vpcore;
    iodev_t     * pdev = NULL;
    int           times = 0;
    char        * localip = "127.0.0.1";

    if (!pcore) return -1;

    if (pcore->informfd == INVALID_SOCKET) {
        pcore->informfd = socket(PF_INET, SOCK_DGRAM, 0);
        sock_nonblock_set(pcore->informfd, 1);
    }

    srandom(time(NULL));
    pcore->informport = ((int)random() * 5739) % 32000;
    if (pcore->informport < 0) pcore->informport *= -1;
    if (pcore->informport < 1000) 
        pcore->informport += 1000; /* get from conf */

    do {

        pcore->informport += 1;
        pcore->wakeupfd = udp_listen(localip, pcore->informport, NULL, NULL, NULL);

    } while (pcore->wakeupfd == INVALID_SOCKET && times++ < 20000);

    sock_nonblock_set(pcore->wakeupfd, 1);

    pcore->wakeupdev = pdev = (iodev_t *)bpool_fetch(pcore->device_pool);
    if (pdev == NULL) {
        pdev = iodev_alloc();
        if (pdev == NULL) return -100;
    }

    pdev->fd = pcore->wakeupfd;
    pdev->fdtype = FDT_UDPSRV;
    pdev->iostate = IOS_READWRITE;
    pdev->rwflag = RWF_READ;

    return 0;
}


int epcore_wakeup_clean (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;

    if (!pcore) return -1;

    closesocket(pcore->informfd);
    pcore->informfd = -1;

    closesocket(pcore->wakeupfd);

    iodev_free(pcore->wakeupdev);
    pcore->wakeupdev = NULL;

    return 0;
}


int epcore_wakeup_send (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    int  ret = 0;
    struct sockaddr_in  addr;

    if (!pcore) return -1;

    //if (!pcore->blocking) return 0;
    //if (pcore->deblock_times++ > 3) return 0;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(pcore->informport);

    ret = sendto (pcore->informfd, "a", 1, 0,
              (struct sockaddr *) &addr, sizeof(addr));

    return ret;
}


int epcore_wakeup_recv (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    uint8 inBuf[1024];
    struct sockaddr_in addr;
    int len = sizeof(addr);
    int ret = 0;
#if defined(_WIN32) || defined(_WIN64)
    u_long  arg = 0;
#endif

    while (1) {
#if defined(_WIN32) || defined(_WIN64)
        arg = 0;
        ret = ioctlsocket(pcore->wakeupfd, FIONREAD, &arg);
        if (ret < 0) break;
        if (arg <= 0) break;

        ret = recvfrom(pcore->wakeupfd, inBuf, sizeof(inBuf), 0,
                 (struct sockaddr *)&addr, (socklen_t *)&len);
#endif
#ifdef UNIX
        ret = recvfrom(pcore->wakeupfd, inBuf, sizeof(inBuf), 0,
                 (struct sockaddr *)&addr, (socklen_t *)&len);
#endif

        if (ret <= 0) {
            break;
        }
        if (ret == 1 && inBuf[0] == 'a') continue;
    }

    return 0;
}

int epcore_wakeup_getmon (void * vpcore, void * veps)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    epump_t  * epump = (epump_t *)veps;
 
#ifdef HAVE_EPOLL
 
    struct epoll_event ev = { 0, {0} };
 
    if (!pcore) return -1;
    if (!epump) return -2;
 
    epump_iodev_add(epump, pcore->wakeupdev);

    ev.data.ptr = pcore->wakeupdev;
    ev.events = EPOLLIN;
    if (epoll_ctl(epump->epoll_fd, EPOLL_CTL_ADD, pcore->wakeupfd, &ev) < 0) {
        if (errno == EEXIST)
            epoll_ctl(epump->epoll_fd, EPOLL_CTL_MOD, pcore->wakeupfd, &ev);
    }
 
    return 0;

#elif defined(HAVE_KQUEUE)

    struct      kevent ev[1];

    if (!pcore) return -1;
    if (!epump) return -2;

    epump_iodev_add(epump, pcore->wakeupdev);
    memset(ev, 0 ,sizeof(ev));

    EV_SET(ev, pcore->wakeupfd, EVFILT_READ, EV_ADD|EV_ENABLE, 0, 0, (void*)pcore->wakeupdev);
    kevent(epump->kqueue_fd, ev, 1, NULL, 0, NULL);

    return 0;

#elif defined(HAVE_SELECT)

    if (!pcore) return -1;
    if (!epump) return -2;
 
    epump_iodev_add(epump, pcore->wakeupdev);

    if (!FD_ISSET(pcore->wakeupfd, &epump->readFds)) {
        FD_SET(pcore->wakeupfd, &epump->readFds);
    }
 
    return 0;
#endif
}

int epump_wakeup_init (void * vepump)
{
    return 0;
}
 
int epump_wakeup_clean (void * vepump)
{
    return 0;
}
 
int epump_wakeup_send (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_send(epump->epcore);
}
 
int epump_wakeup_recv (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_recv(epump->epcore);
}

#endif

