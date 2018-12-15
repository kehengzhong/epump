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
#include "ioevent.h"


void * ephware_bind_fd (void * vpcore, int fd, void * para, IOHandler * ioh, void * iohpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
 
    if (!pcore) return NULL;
    if (fd == INVALID_SOCKET) return NULL;
 
    //pdev = epcore_iodev_get(pcore, fd);
    if (!pdev) pdev = iodev_new(pcore);
    if (!pdev) return NULL;
 
    pdev->fd = fd;
    pdev->fdtype = FDT_HWARE;

    pdev->para = para;
    pdev->callback = ioh;
    pdev->cbpara = iohpara;
 
    pdev->iostate = IOS_READWRITE;

    iodev_rwflag_set(pdev, RWF_READ);
 
    return pdev;
}

void * ephware_bind_stdin (void * vpcore, void * para, IOHandler * ioh, void * iohpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    int        fd = 0;
 
    if (!pcore) return NULL;
 
    //pdev = epcore_iodev_get(pcore, fd);
    if (!pdev) pdev = iodev_new(pcore);
    if (!pdev) return NULL;
 
    pdev->fd = fd;
    pdev->fdtype = FDT_STDIN;

    pdev->para = para;
    pdev->callback = ioh;
    pdev->cbpara = iohpara;
 
    pdev->iostate = IOS_READWRITE;

    iodev_rwflag_set(pdev, RWF_READ);
 
    return pdev;
}

int epstdin_callback (void * vpcore, void * pobj, int event, int fdtype)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = (iodev_t *)pobj;
    char       buf[512];
    char     * p = NULL;
 
    if (!pcore || !pdev) return 0;
 
    if (event == IOE_READ && fdtype == FDT_STDIN) {
        memset(buf, 0, sizeof(buf));
        fgets((char *)buf, sizeof(buf), stdin);
        if (strlen(buf) <= 0) return 0;
        for (p = buf; *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'; p++);
        if (strlen((char *)p) <= 0) return 0;
 
        if (strncasecmp(p, "show", 4) == 0) {
            iodev_print(pdev);
        } else if (strncasecmp(p, "quit", 4) == 0) {
        }
    } else if (event == IOE_INVALID_DEV) {
        return -100;
    }
 
    return 0;
}
 
