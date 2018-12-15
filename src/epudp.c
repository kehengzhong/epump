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

 
void * epudp_listen (void * vpcore, char * localip, uint16 port,
                     void * para, int * retval, IOHandler * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
 
    if (retval) *retval = -1;
    if (!pcore) return NULL;
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -100;
        return NULL;
    }
 
    pdev->fd = epm_udp_listen(localip, port);
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
 
    pdev->iostate = IOS_READWRITE;
    pdev->fdtype = FDT_UDPSRV;
 
    iodev_rwflag_set(pdev, RWF_READ);
 
    if (retval) *retval = 0;
    return pdev;
}
 
void * epudp_client (void * vpcore, char * localip, uint16 port,
                     void * para, int * retval, IOHandler * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
 
    if (retval) *retval = -1;
    if (!pcore) return NULL;
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -100;
        return NULL;
    }
 
    if (port == 0)
        pdev->fd = socket(AF_INET, SOCK_DGRAM, 0);
    else
        pdev->fd = epm_udp_listen(localip, port);
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
 
    pdev->iostate = IOS_READWRITE;
    pdev->fdtype = FDT_UDPCLI;
 
    iodev_rwflag_set(pdev, RWF_READ);
 
    if (retval) *retval = 0;
    return pdev;
}
 
