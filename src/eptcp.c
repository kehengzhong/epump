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
#include "epm_sock.h"

#include "epcore.h"
#include "epump_internal.h"
#include "iodev.h"

void * eptcp_listen (void * vpcore, uint16 port, void * para, int * retval,
                     IOHandler * cb, void * cbpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
 
    if (retval) *retval = -1;
    if (!pcore) return NULL;
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -100;
        return NULL;
    }
 
    pdev->fd = epm_tcp_listen(NULL, port, NULL);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -200;
        return NULL;
    }
 
    epm_sock_nonblock_set(pdev->fd, 1);
 
    pdev->local_port = port;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    pdev->iostate = IOS_ACCEPTING;
    pdev->fdtype = FDT_LISTEN;
 
    iodev_rwflag_set(pdev, RWF_READ);
 
    if (retval) *retval = 0;
    return pdev;
}
 
 
void * eptcp_accept (void * vpcore, void * vld, void * para, int * retval,
                     IOHandler * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * listendev = (iodev_t *)vld;
    iodev_t   * pdev = NULL;
    socklen_t   addrlen;
    SOCKET      clifd;
    struct sockaddr cliaddr;
    struct sockaddr sock;
 
    if (retval) *retval = -1;
    if (!pcore || !listendev) return NULL;
 
    addrlen = sizeof(cliaddr);

    EnterCriticalSection(&listendev->fdCS);
    clifd = accept(listendev->fd, (struct sockaddr *)&cliaddr, (socklen_t *)&addrlen);
    LeaveCriticalSection(&listendev->fdCS);

    if (clifd == INVALID_SOCKET) {
        if (retval) *retval = -100;
        return NULL;
    }
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -200;
#ifdef UNIX
        shutdown(clifd, SHUT_RDWR);
#endif
#ifdef _WIN32
        shutdown(clifd, 0x02);//SD_RECEIVE=0x00, SD_SEND=0x01, SD_BOTH=0x02);
#endif
        closesocket(clifd);
        return NULL;
    }
 
    pdev->fd = clifd;
    epm_sock_addr_ntop(&cliaddr, pdev->remote_ip);
    pdev->remote_port = epm_sock_addr_port(&cliaddr);
 
    epm_sock_nonblock_set(pdev->fd, 1);
 
    addrlen = sizeof(sock);
    if (getsockname(pdev->fd, (struct sockaddr *)&sock, (socklen_t *)&addrlen) == 0) {
        epm_sock_addr_ntop(&sock, pdev->local_ip);
        pdev->local_port = epm_sock_addr_port(&sock);
    }
    pdev->local_port = listendev->local_port;
 
    pdev->fdtype = FDT_ACCEPTED;
    pdev->iostate = IOS_READWRITE;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    iodev_rwflag_set(pdev, RWF_READ);

    if (retval) *retval = 0;

    return pdev;
}
 
void * eptcp_connect (void * vpcore, char * host, int port,
                      char * localip, int localport, void * para,
                      int * retval, IOHandler * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
    int         succ = 0;
    socklen_t   addrlen;
    struct sockaddr sock;
 
    if (retval) *retval = -1;
    if (!pcore) return NULL;
 
    if (!host) {
        if (retval) *retval = -10;
        return NULL;
    }
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -20;
        return NULL;
    }
 
    pdev->fdtype = FDT_CONNECTED;
    pdev->para = para;
 
    if (cb) {
        pdev->callback = cb;
        pdev->cbpara = cbpara;
    }
 
    pdev->fd = epm_tcp_nb_connect(host, port, localip, localport, &succ);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -30;
        return NULL;
    }
 
    addrlen = sizeof(sock);
    if (getsockname(pdev->fd, (struct sockaddr *)&sock, (socklen_t *)&addrlen) == 0) {
        epm_sock_addr_ntop(&sock, pdev->local_ip);
        pdev->local_port = epm_sock_addr_port(&sock);
    }
    addrlen = sizeof(sock);
    if (getpeername(pdev->fd, (struct sockaddr *)&sock, (socklen_t *)&addrlen) == 0) {
        epm_sock_addr_ntop(&sock, pdev->remote_ip);
        pdev->remote_port = epm_sock_addr_port(&sock);
    }
 
    if (succ > 0) { /* connect successfully */
        pdev->iostate = IOS_READWRITE;
        if (retval) *retval = 0;

        iodev_rwflag_set(pdev, RWF_READ);
        return pdev;
    }
 
    pdev->iostate = IOS_CONNECTING;
    iodev_rwflag_set(pdev, RWF_READ | RWF_WRITE);
 
    if (retval) *retval = -100;
    return pdev;
}
 
