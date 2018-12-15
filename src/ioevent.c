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

#include "epcore.h"
#include "epump_internal.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"


int ioevent_free (void * vioe)
{
    ioevent_t * ioe = (ioevent_t *)vioe;

    if (!ioe) return -1;

    epm_free(ioe);
    return 0;
}


static int
ioevent_push_event (epump_t * epump, ioevent_t * ioe)
{
    int  num;
    int  curt = 0;
    ioevent_t * firstioe = NULL;

    if (!epump) return -1;
    if (!ioe) return -2;

    EnterCriticalSection(&epump->ioeventlistCS);
    curt = time(&ioe->stamp);
    epm_lt_append(epump->ioevent_list, ioe);
    num = epm_lt_num(epump->ioevent_list);
    firstioe = epm_lt_first(epump->ioevent_list);
    LeaveCriticalSection(&epump->ioeventlistCS);

#ifdef _DEBUG
{
    char title[128];
    sprintf(title, "NewEvent[%d]: %lu ", num, epump->threadid);
    ioevent_print(ioe, title);
}
#endif

    return 0;
}


int ioevent_push (void * vepump, int event, void * obj, void * cb, void * cbpara)
{
    epump_t   * epump = (epump_t *)vepump;
    ioevent_t * ioe = NULL;

    if (!epump) return -1;

    ioe = (ioevent_t *)epm_pool_fetch(epump->epcore->event_pool);
    if (!ioe) return -10;

    ioe->type = event;
    ioe->obj = obj;
    ioe->callback = cb;
    ioe->cbpara = cbpara;

    ioe->objid = 0;
    if (obj && event < IOE_TIMEOUT) { //including all iodev_t objects
        ioe->objid = ((iodev_t *)obj)->id;
    }

    return ioevent_push_event(epump, ioe);
}

ioevent_t * ioevent_pop (void * vepump)
{
    epump_t   * epump = (epump_t *)vepump;
    ioevent_t * ioe = NULL;
    int         ret = 0;
    int         i = 0;
    GeneralCB * gcb = NULL;

    EnterCriticalSection(&epump->ioeventlistCS);
    ioe = epm_lt_rm_head(epump->ioevent_list);
    LeaveCriticalSection(&epump->ioeventlistCS);
    if (ioe) return ioe;

    EnterCriticalSection(&epump->exteventlistCS);
    if ((ret = epm_arr_num(epump->exteventlist)) > 0) {
        for (i = 0; i < ret; i++) {
            if (epump->exteventindex >= (int)ret) epump->exteventindex = 0;
            ioe = epm_arr_value(epump->exteventlist, epump->exteventindex);
            ++epump->exteventindex;
            if (ioe) {
                gcb = (GeneralCB *)ioe->ignitor;
                if (gcb && (ioe->ignresult = (*gcb)(ioe->igpara, ioe->type)) >= 0) {
                    LeaveCriticalSection(&epump->exteventlistCS);
                    return ioe;
                }
            }
        }
    }
    LeaveCriticalSection(&epump->exteventlistCS);

    return NULL;
}

int ioevent_handle (void * vepump)
{
    epump_t    * epump = (epump_t *)vepump;
    epcore_t   * pcore = NULL;
    ioevent_t  * ioe = NULL;
    iodev_t    * pdev = NULL;
    iodev_t    * ptmp = NULL;
    iotimer_t  * piot = NULL;
    int          ret = 0;
    GeneralCB  * gcb = NULL;
    IOHandler  * iocb = NULL;
    int          evnum = 0;
    ulong        curid = 0;

    if (!epump) return -1;

    pcore = (epcore_t *)epump->epcore;
    if (!pcore) return -2;

    while (!epump->quit) {
        ioe = ioevent_pop(epump);
        if (!ioe) break;

        if (ioe->externflag == 1) {
            gcb = (GeneralCB *)ioe->callback;
            if (!gcb) continue;
 
            (*gcb)(ioe->obj, ioe->ignresult);
            evnum++;
            continue;
        } else if (ioe->externflag == 2) {
            /* user-generated events */
            iocb = (IOHandler *)ioe->callback;
            if (iocb)
                ret = (*iocb)(ioe->cbpara, ioe->obj, ioe->type, FDT_USERCMD);
            else if (pcore->callback)
                ret = (*pcore->callback)(pcore->cbpara, ioe->obj, ioe->type, FDT_USERCMD);
 
            /* recycle event to pool */
            epm_pool_recycle(pcore->event_pool, ioe);
            ioe = NULL;
            evnum++;
 
            continue;
        }

        if (ioe->objid > 0 && epcore_iodev_find(pcore, ioe->objid) != ioe->obj) {
            epm_pool_recycle(pcore->event_pool, ioe);
            ioe = NULL;
            continue;
        }

        switch (ioe->type) {
        case IOE_CONNECTED:
        case IOE_CONNFAIL:
        case IOE_ACCEPT:
        case IOE_READ:
        case IOE_WRITE:
            pdev = (iodev_t *)ioe->obj;
            if (pdev->fd == INVALID_SOCKET || epcore_iodev_find(pcore, pdev->id) == NULL)
                break;

            if (ioe->type == IOE_CONNECTED || ioe->type == IOE_CONNFAIL || ioe->type == IOE_ACCEPT)
                pdev->iostate = IOS_READWRITE;
            curid = pdev->id;

            if (pdev->callback)
                ret = (*pdev->callback)(pdev->cbpara, pdev, ioe->type, pdev->fdtype);
            else if (pcore->callback)
                ret = (*pcore->callback)(pcore->cbpara, pdev, ioe->type, pdev->fdtype);
 
            ptmp = epcore_iodev_find(pcore, curid);
            if (ptmp && ptmp->fd != INVALID_SOCKET) {
                //iodev_add_notify(ptmp, ptmp->rwflag | RWF_READ);
            }
            break;
 
        case IOE_TIMEOUT:
            piot = (iotimer_t *)ioe->obj;
            if (piot->cmdid == IOTCMD_IDLE) { //system inner timeout
                /* the device in the idle table has enough time not to be in use,
                 * system should discard it from the idle table: close the connection,
                 * recycle the device resource etc. */
 
                pdev = (iodev_t *)piot->para;
                if (pdev && piot == (iotimer_t *)pdev->iot) {
                    iodev_close(pdev);
                }
            } else {
                if (piot->callback)
                    (*piot->callback)(piot->cbpara, piot, ioe->type, FDT_TIMER);
                else if (pcore->callback)
                    (*pcore->callback)(pcore->cbpara, piot, ioe->type, FDT_TIMER);
            }
            iotimer_recycle(piot);
            break;
 
        case IOE_INVALID_DEV:
            pdev = (iodev_t *)ioe->obj;
            if (pdev->fd == INVALID_SOCKET || epcore_iodev_find(pcore, pdev->id) == NULL)
                break;
 
            curid = pdev->id;
 
            if (pdev->callback)
                ret = (*pdev->callback)(pdev->cbpara, pdev, ioe->type, pdev->fdtype);
            else if (pcore->callback)
                ret = (*pcore->callback)(pcore->cbpara, pdev, ioe->type, pdev->fdtype);
 
            ptmp = epcore_iodev_find(pcore, curid);
            if (ptmp && ptmp->fd != INVALID_SOCKET) {
                //iodev_add_notify(ptmp, ptmp->rwflag);
            } else if (ptmp)
                iodev_close(ptmp);
            break;
 
        default:
            iocb = (IOHandler *)ioe->callback;
            if (iocb)
                ret = (*iocb)(ioe->cbpara, ioe->obj, ioe->type, FDT_USERCMD);
            else if (pcore->callback)
                ret = (*pcore->callback)(pcore->cbpara, ioe->obj, ioe->type, FDT_USERCMD);
            break;
        }
 
        /* recycle event to pool */
        epm_pool_recycle(pcore->event_pool, ioe);
        ioe = NULL;
        evnum++;
    }

    return evnum;
}

void ioevent_print (void * vioe, char * title)
{
#ifdef _DEBUG
    ioevent_t  * ioe = (ioevent_t *)vioe;
    iodev_t    * pdev = NULL;
    char         trline[256];

    if (!ioe) return;

    trline[0] = '\0';

    if (title) sprintf(trline+strlen(trline), title);

    if (ioe->type == IOE_CONNECTED) sprintf(trline+strlen(trline), "IOE_CONNECTED");
    else if (ioe->type == IOE_CONNFAIL) sprintf(trline+strlen(trline), "IOE_CONNFAIL");
    else if (ioe->type == IOE_ACCEPT) sprintf(trline+strlen(trline), "IOE_ACCEPT");
    else if (ioe->type == IOE_READ) sprintf(trline+strlen(trline), "IOE_READ");
    else if (ioe->type == IOE_WRITE) sprintf(trline+strlen(trline), "IOE_WRITE");
    else if (ioe->type == IOE_TIMEOUT) sprintf(trline+strlen(trline), "IOE_TIMEOUT");
    else if (ioe->type == IOE_INVALID_DEV) sprintf(trline+strlen(trline), "IOE_INVALID_DEV");
    else sprintf(trline+strlen(trline), "Unknown");

    if (ioe->type != IOE_TIMEOUT) {
        sprintf(trline+strlen(trline), " ");
        pdev = (iodev_t *)ioe->obj;
        if (pdev->fdtype == FDT_LISTEN) sprintf(trline+strlen(trline), "FDT_LISTEN");
        else if (pdev->fdtype == FDT_CONNECTED) sprintf(trline+strlen(trline), "FDT_CONNECTED");
        else if (pdev->fdtype == FDT_ACCEPTED) sprintf(trline+strlen(trline), "FDT_ACCEPTED");
        else if (pdev->fdtype == FDT_UDPSRV) sprintf(trline+strlen(trline), "FDT_UDPSRV");
        else if (pdev->fdtype == FDT_UDPCLI) sprintf(trline+strlen(trline), "FDT_UDPCLI");
        else if (pdev->fdtype == FDT_RAWSOCK) sprintf(trline+strlen(trline), "FDT_RAWSOCK");
        else if (pdev->fdtype == FDT_TIMER) sprintf(trline+strlen(trline), "FDT_TIMER");
        else if (pdev->fdtype == FDT_LINGER_CLOSE) sprintf(trline+strlen(trline), "FDT_LINGER_CLOSE");
        else if (pdev->fdtype == FDT_STDIN) sprintf(trline+strlen(trline), "FDT_STDIN");
        else if (pdev->fdtype == FDT_STDOUT) sprintf(trline+strlen(trline), "FDT_STDOUT");
        else if (pdev->fdtype == FDT_USOCK_LISTEN) sprintf(trline+strlen(trline), "FDT_USOCK_LISTEN");
        else if (pdev->fdtype == FDT_USOCK_CONNECTED) sprintf(trline+strlen(trline), "FDT_USOCK_CONNECTED");
        else if (pdev->fdtype == FDT_USOCK_ACCEPTED) sprintf(trline+strlen(trline), "FDT_USOCK_ACCEPTED");
        else sprintf(trline+strlen(trline), "Unknown Type");

        sprintf(trline+strlen(trline), " FD=%d Remote:<%s:%d> Local:<%s:%d>",
                 pdev->fd, pdev->remote_ip, pdev->remote_port,
                 pdev->local_ip, pdev->local_port);
    } else {
        if (ioe->obj) {
            sprintf(trline+strlen(trline), " CmdID=%d", ((iotimer_t *)ioe->obj)->cmdid);
        }
    }
    printf("%s\n", trline);
#endif
}

