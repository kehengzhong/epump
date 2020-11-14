/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */
 
#include "btype.h"
#include "bpool.h"
#include "tsock.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "epwakeup.h"

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
#else

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
    struct epoll_event ev = { 0, {0} };
 
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
 
    epump_iodev_add(epump, epump->wakeupdev);
 
    ev.data.ptr = epump->wakeupdev;
    ev.events = EPOLLIN;
    if (epoll_ctl(epump->epoll_fd, EPOLL_CTL_ADD, epump->wakeupfd, &ev) < 0) {
        if (errno == EEXIST)
            epoll_ctl(epump->epoll_fd, EPOLL_CTL_MOD, epump->wakeupfd, &ev);
    }

    return 0;
}
 
int epump_wakeup_clean (void * vepump)
{
    epump_t  * epump = (epump_t *)vepump;
    struct epoll_event ev = { 0, {0} };
 
    if (!epump) return -1;
 
    if (epump_iodev_del(epump, epump->wakeupfd) != NULL)
        epoll_ctl(epump->epoll_fd, EPOLL_CTL_DEL, epump->wakeupfd, &ev);

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
        pcore->wakeupfd = udp_listen(localip, pcore->informport);

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

    return 0;
}


int epcore_wakeup_recv (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    uint8 inBuf[1024];
    struct sockaddr_in addr;
    int len = sizeof(addr);
    int ret = 0;
#ifdef _WIN32
    u_long  arg = 0;
#endif

    while (1) {
#ifdef _WIN32
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
 
    ev.data.ptr = pcore->wakeupdev;
    ev.events = EPOLLIN;
    if (epoll_ctl(epump->epoll_fd, EPOLL_CTL_ADD, pcore->wakeupfd, &ev) < 0) {
        if (errno == EEXIST)
            epoll_ctl(epump->epoll_fd, EPOLL_CTL_MOD, pcore->wakeupfd, &ev);
    }
 
    return 0;
#else
 
    if (!pcore) return -1;
    if (!epump) return -2;
 
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

