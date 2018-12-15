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

#ifdef UNIX

#include "epm_sock.h"

#include "epcore.h"
#include "epump_internal.h"
#include "iodev.h"


void * epusock_connect (void * vpcore, char * sockname, void * para,
                        int * retval, IOHandler * ioh, void * iohpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
 
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

    pdev->fd = epm_usock_connect(sockname);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -30;
        return NULL;
    }
 
    epm_sock_nonblock_set(pdev->fd, 1);
 
    pdev->iostate = IOS_READWRITE;

    iodev_rwflag_set(pdev, RWF_READ);
 
    if (retval) *retval = 0;
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
 
    pdev->fd = epm_usock_create(sockname);
    if (pdev->fd < 0) {
        iodev_close(pdev);
        if (retval) *retval = -200;
        return NULL;
    }
 
    epm_sock_nonblock_set(pdev->fd, 1);
 
    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    pdev->iostate = IOS_ACCEPTING;
    pdev->fdtype = FDT_USOCK_LISTEN;
 
    iodev_rwflag_set(pdev, RWF_READ);
 
    if (retval) *retval = 0;
    return pdev;
}
 
 
void * epusock_accept (void * vpcore, void * vld, void * para, int * retval,
                       IOHandler * cb, void * cbpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * listendev = (iodev_t *)vld;
    iodev_t  * pdev = NULL;
    int        clifd;
 
    if (retval) *retval = -1;
    if (!pcore || !listendev) return NULL;
 
    clifd = epm_usock_accept(listendev->fd);
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
 
    pdev->fd = clifd;
 
    epm_sock_nonblock_set(pdev->fd, 1);
 
    pdev->fdtype = FDT_USOCK_ACCEPTED;
    pdev->iostate = IOS_READWRITE;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;

    iodev_rwflag_set(pdev, RWF_READ);

    if (retval) *retval = 0;
    return pdev;
}

#endif

