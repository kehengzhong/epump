/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */
 
#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "epwakeup.h"

#ifdef HAVE_EVENTFD
#include <sys/eventfd.h>
#endif


int epcore_wakeup_init (void * vpcore)
{
#if defined(HAVE_IOCP)
    return 0;

#elif defined(HAVE_EVENTFD)
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;

    if (!pcore) return -1;

    pcore->wakeupfd = eventfd(0, 0);
    sock_nonblock_set(pcore->wakeupfd, 1);
    pcore->wakeupdev = pdev = iodev_new_from_fd(pcore, pcore->wakeupfd, FDT_FILEDEV, NULL, NULL, NULL);

    iodev_bind_epump(pcore->wakeupdev, BIND_ALL_EPUMP, NULL);
    return 0;

#elif defined(WAKE_BY_UDP)
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
    int         times = 0;
    char      * localip = "127.0.0.1";

    if (!pcore) return -1;

    if (pcore->informfd == INVALID_SOCKET) {
        pcore->informfd = socket(PF_INET, SOCK_DGRAM, 0);
        sock_nonblock_set(pcore->informfd, 1);
    }

    srandom(time(NULL));
    pcore->informport = ((int)random() * 5739) % 32000;
    if (pcore->informport < 0) pcore->informport *= -1;
    if (pcore->informport < 1000) pcore->informport += 1000;

    do {
        pcore->informport += 1;
        pcore->wakeupfd = udp_listen(localip, pcore->informport, NULL, NULL, NULL);
    } while (pcore->wakeupfd == INVALID_SOCKET && times++ < 20000);

    sock_nonblock_set(pcore->wakeupfd, 1);

    pcore->wakeupdev = pdev = iodev_new_from_fd(pcore, pcore->wakeupfd, FDT_UDPSRV, NULL, NULL, NULL);

    iodev_bind_epump(pcore->wakeupdev, BIND_ALL_EPUMP, NULL);
    return 0;

#else
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
    SOCKET      fds[2];

    if (!pcore) return -1;

    //if (pipe_create(fds) < 0)
    if (sock_pair_create(SOCK_STREAM, fds) < 0)
        return -2;

    pcore->wakeupfd = fds[0];
    pcore->informfd = fds[1];

    sock_nonblock_set(pcore->informfd, 1);
    sock_nonblock_set(pcore->wakeupfd, 1);

    pcore->wakeupdev = pdev = iodev_new_from_fd(pcore, pcore->wakeupfd, FDT_CONNECTED, NULL, NULL, NULL);

    iodev_bind_epump(pcore->wakeupdev, BIND_ALL_EPUMP, NULL);
    return 0;
#endif
}

int epcore_wakeup_clean (void * vpcore)
{
#if defined(HAVE_IOCP)
    return 0;

#elif defined(HAVE_EVENTFD)
    epcore_t * pcore = (epcore_t *)vpcore;
 
    if (!pcore) return -1;

    if (pcore->wakeupdev) { 
        iodev_close(pcore->wakeupdev);
        pcore->wakeupdev = NULL;
    }
    pcore->wakeupfd = INVALID_SOCKET;

    return 0;

#else
    epcore_t * pcore = (epcore_t *)vpcore;
 
    if (!pcore) return -1;

    if (pcore->wakeupdev) { 
        iodev_close(pcore->wakeupdev);
        pcore->wakeupdev = NULL;
    }
    pcore->wakeupfd = INVALID_SOCKET;

    closesocket(pcore->informfd);
    pcore->informfd = INVALID_SOCKET;

    return 0;

#endif
}

int epcore_wakeup_send (void * vpcore)
{
    epcore_t * pcore = (epcore_t *)vpcore;

#if defined(HAVE_IOCP)
    int        i;

    if (!pcore) return -1;

    for (i = 0; i < arr_num(pcore->epump_list); i++) {
        PostQueuedCompletionStatus(pcore->iocp_port, 0, (ULONG_PTR)-1, NULL);
    }

    return 0;

#elif defined(HAVE_EVENTFD)
    uint64     val = 1;

    if (!pcore) return -1;

    write(pcore->wakeupfd, &val, sizeof(val));

    return 0;

#elif defined(WAKE_BY_UDP)
    int  ret = 0;
    struct sockaddr_in  addr;

    if (!pcore) return -1;

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(pcore->informport);

    ret = sendto (pcore->informfd, "a", 1, 0,
              (struct sockaddr *) &addr, sizeof(addr));

    return ret;

#else
    if (!pcore) return -1;

    write(pcore->wakeupfd, "a", 1);

    return 0;
#endif
}

int epcore_wakeup_recv (void * vpcore)
{
#if defined(HAVE_IOCP)
    return 0;

#elif defined(HAVE_EVENTFD)
    epcore_t * pcore = (epcore_t *)vpcore;
    uint64     val = 0;
 
    if (!pcore) return -1;
 
    read(pcore->wakeupfd, &val, sizeof(val));

    return 0;

#elif defined(WAKE_BY_UDP)
    epcore_t           * pcore = (epcore_t *)vpcore;
    uint8                inBuf[1024];
    struct sockaddr_in   addr;
    int                  len = sizeof(addr);
    int                  ret = 0;
#if defined(_WIN32) || defined(_WIN64)
    u_long               arg = 0;
    int                  errcode = 0;
#endif
 
    while (1) {

#if defined(_WIN32) || defined(_WIN64)
        arg = 0;
        ret = ioctlsocket(pcore->wakeupfd, FIONREAD, &arg);
        if (ret < 0) break;
        if (arg <= 0) break;
 
        ret = recvfrom(pcore->wakeupfd, inBuf, sizeof(inBuf), 0,
                 (struct sockaddr *)&addr, (socklen_t *)&len);
        if (ret < 0) {
            errcode = WSAGetLastError();
            if (errcode != 0 && errcode != WSAEINTR && errcode != WSAEWOULDBLOCK) {
                epcore_wakeup_clean(pcore);
                epcore_wakeup_init(pcore);
            } 
            break;
        }
#endif

#ifdef UNIX
        ret = recvfrom(pcore->wakeupfd, inBuf, sizeof(inBuf), 0,
                 (struct sockaddr *)&addr, (socklen_t *)&len);
        if (ret <= 0) {
            if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                epcore_wakeup_clean(pcore);
                epcore_wakeup_init(pcore);
            } 
            break;
        }
#endif

        if (ret == 1 && inBuf[0] == 'a') continue;
    }
 
    return 0;

#else
    epcore_t * pcore = (epcore_t *)vpcore;
    char       buf[32];
 
    if (!pcore) return -1;
 
    read(pcore->wakeupfd, buf, 32);

    return 0;
#endif
}



int epump_wakeup_init (void * vepump)
{
#if defined(HAVE_IOCP)
    return 0;
 
#elif defined(HAVE_EVENTFD)
    epump_t   * epump = (epump_t *)vepump;
    iodev_t   * pdev = NULL;
 
    if (!epump) return -1;
 
    epump->wakeupfd = eventfd(0, 0);
    sock_nonblock_set(epump->wakeupfd, 1);
    epump->wakeupdev = pdev = iodev_new_from_fd(epump->epcore, epump->wakeupfd, FDT_FILEDEV, NULL, NULL, NULL);
 
    iodev_bind_epump(epump->wakeupdev, BIND_GIVEN_EPUMP, epump);
    return 0;

#else
    return 0;

#endif
}
 
int epump_wakeup_clean (void * vepump)
{
#if defined(HAVE_IOCP)
    return 0;
 
#elif defined(HAVE_EVENTFD)
    epump_t  * epump = (epump_t *)vepump;

    if (!epump) return -1;

    if (epump->wakeupdev) { 
        iodev_close(epump->wakeupdev);
        epump->wakeupdev = NULL;
    }
    epump->wakeupfd = INVALID_SOCKET;

    return 0;
#else
    return 0;
#endif
}
 
int epump_wakeup_send (void * vepump)
{
#if defined(HAVE_IOCP)
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_send(epump->epcore);

#elif defined(HAVE_EVENTFD)
    epump_t  * epump = (epump_t *)vepump;
    uint64     val = 1;
 
    if (!epump) return -1;
 
    write(epump->wakeupfd, &val, sizeof(val));
 
    return 0;

#else
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_send(epump->epcore);
#endif
}
 
int epump_wakeup_recv (void * vepump)
{
#if defined(HAVE_IOCP)
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_recv(epump->epcore);

#elif defined(HAVE_EVENTFD)
    epump_t  * epump = (epump_t *)vepump;
    uint64     val = 0;
 
    if (!epump) return -1;
 
    read(epump->wakeupfd, &val, sizeof(val));
 
    return 0;

#else
    epump_t  * epump = (epump_t *)vepump;
 
    if (!epump) return -1;
 
    return epcore_wakeup_recv(epump->epcore);

#endif
}
 
