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
#include "epm_pool.h"
#include "epm_sock.h"

#include "epcore.h"
#include "epump_internal.h"
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
    epm_sock_nonblock_set(pcore->wakeupfd, 1);

    pcore->wakeupdev = pdev = (iodev_t *)epm_pool_fetch(pcore->device_pool);
    if (pdev == NULL) {
        pdev = iodev_alloc();
        if (pdev == NULL) return -100;
    }
    pdev->fd = pcore->wakeupfd;
    pdev->fdtype = FDT_HWARE;
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

    ev.data.fd = pcore->wakeupfd;
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

#else

int epcore_wakeup_init (void * vpcore)
{
    epcore_t    * pcore = (epcore_t *)vpcore;
    iodev_t     * pdev = NULL;
    int           times = 0;
    struct in_addr localip;

    if (!pcore) return -1;

    if (pcore->informfd == INVALID_SOCKET) {
        pcore->informfd = socket(PF_INET, SOCK_DGRAM, 0);
        epm_sock_nonblock_set(pcore->informfd, 1);
    }

    srandom(time(NULL));
    pcore->informport = ((int)random() * 5739) % 32000;
    if (pcore->informport < 0) pcore->informport *= -1;
    if (pcore->informport < 1000) 
        pcore->informport += 1000; /* get from conf */

    localip.s_addr = inet_addr("127.0.0.1");
    do {
        pcore->informport += 1;
        pcore->wakeupfd = epm_udp_listen("127.0.0.1", (uint16)pcore->informport);
    } while (pcore->wakeupfd == INVALID_SOCKET && times++ < 20000);
    epm_sock_nonblock_set(pcore->wakeupfd, 1);

#ifdef _DEBUG
printf("Wakeup: notify port = %d\n", pcore->informport);
#endif

    pcore->wakeupdev = pdev = (iodev_t *)epm_pool_fetch(pcore->device_pool);
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

#endif

