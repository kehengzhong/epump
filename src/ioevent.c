/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "bpool.h"
#include "mthread.h"
#include "memory.h"

#include "epcore.h"
#include "epump_local.h"
#include "worker.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epdns.h"

#ifdef HAVE_IOCP
#include "epiocp.h"
#endif


int ioevent_free (void * vioe)
{
    ioevent_t * ioe = (ioevent_t *)vioe;

    if (!ioe) return -1;

    kfree(ioe);
    return 0;
}


int epump_ioevent_push (epump_t * epump, ioevent_t * ioe)
{
    if (!epump) return -1;
    if (!ioe) return -2;

    EnterCriticalSection(&epump->ioeventlistCS);
    lt_append(epump->ioevent_list, ioe);
    LeaveCriticalSection(&epump->ioeventlistCS);

    return 0;
}

int ioevent_dispatch (void * vepump, void * vioe)
{
    epump_t    * epump = (epump_t *)vepump;
    worker_t   * wker = NULL;
    ioevent_t  * ioe = (ioevent_t *)vioe;
    epcore_t   * pcore = NULL;
    iodev_t    * pdev = NULL;
    iotimer_t  * piot = NULL;
    DnsMsg     * dnsmsg = NULL;
    ulong        threadid = 0;
    uint8        newchoice = 0;
 
    if (!epump || !ioe) return -1;
 
    pcore = (epcore_t *)epump->epcore;
    if (!pcore) return -2;

    switch (ioe->type) {
    case IOE_CONNECTED:
    case IOE_CONNFAIL:
    case IOE_READ:
    case IOE_WRITE:
    case IOE_INVALID_DEV:
        pdev = (iodev_t *)ioe->obj;
        if (!pdev || pdev->fd == INVALID_SOCKET) {
            bpool_recycle(pcore->event_pool, ioe);
            return -100;
        }
        ioe->objid = pdev->id;

        threadid = pdev->threadid;
        break;

    /* when ListenDev accepts a connect request, we keep its threadid value 0.
       it causes the lowest worker thread will be assigned for event handling */
    case IOE_ACCEPT:
        pdev = (iodev_t *)ioe->obj;
        if (!pdev || pdev->fd == INVALID_SOCKET) {
            bpool_recycle(pcore->event_pool, ioe);
            return -100;
        }

        ioe->objid = pdev->id;
        break;

    case IOE_TIMEOUT:
        piot = (iotimer_t *)ioe->obj;
        if (!piot) {
            bpool_recycle(pcore->event_pool, ioe);
            return -101;
        }

        threadid = piot->threadid;
        break;

    case IOE_DNS_RECV:
        dnsmsg = (DnsMsg *)ioe->obj;
        if (!dnsmsg) {
            bpool_recycle(pcore->event_pool, ioe);
            return -101;
        }

        threadid = dnsmsg->threadid;
        break;

    case IOE_USER_DEFINED:
        threadid = get_threadid();
        break;
    }

    if (threadid > 0)
        wker = worker_thread_find(pcore, threadid);

    if (!wker) {
        wker = worker_thread_select(pcore);
        newchoice = 1;
    }

    if (wker) {
        if (pdev && (pdev->threadid < 10 || newchoice)) {
            pdev->threadid = wker->threadid;
        }
        if (piot) piot->threadid = wker->threadid;
        if (dnsmsg) dnsmsg->threadid = wker->threadid;

        ioe->workerid = wker->threadid;

        return worker_ioevent_push(wker, ioe);

    } else {
        if (pdev) pdev->threadid = 0;
        if (piot) piot->threadid = 0;
        if (dnsmsg) dnsmsg->threadid = 0;

        return epump_ioevent_push(epump, ioe);
    }
}


int ioevent_push (void * vepump, int event, void * obj, void * cb, void * cbpara)
{
    epump_t   * epump = (epump_t *)vepump;
    ioevent_t * ioe = NULL;

    if (!epump) return -1;

    ioe = (ioevent_t *)bpool_fetch(epump->epcore->event_pool);
    if (!ioe) return -10;

    ioe->type = event;
    ioe->obj = obj;
    ioe->callback = cb;
    ioe->cbpara = cbpara;

    ioe->objid = 0;

    ioe->epumpid = epump->threadid;
    ioe->workerid = 0;

    EnterCriticalSection(&epump->epcore->eventnumCS);
    epump->epcore->acc_event_num++;
    LeaveCriticalSection(&epump->epcore->eventnumCS);

    if (arr_num(epump->epcore->worker_list) <= 0)
        return epump_ioevent_push(epump, ioe);

    /* find a worker thread and dispatch the event to worker event queue. */

    return ioevent_dispatch(epump, ioe);
}

void * ioevent_pop (void * vepump)
{
    epump_t   * epump = (epump_t *)vepump;
    ioevent_t * ioe = NULL;
    int         ret = 0;
    int         i = 0;
    GeneralCB * gcb = NULL;

    EnterCriticalSection(&epump->ioeventlistCS);
    ioe = lt_rm_head(epump->ioevent_list);
    LeaveCriticalSection(&epump->ioeventlistCS);
    if (ioe) return ioe;

    EnterCriticalSection(&epump->exteventlistCS);
    if ((ret = arr_num(epump->exteventlist)) > 0) {
        for (i = 0; i < ret; i++) {
            if (epump->exteventindex >= (int)ret) epump->exteventindex = 0;
            ioe = arr_value(epump->exteventlist, epump->exteventindex);
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

int ioevent_remove (void * vepump, void * obj)
{ 
    epump_t    * epump = (epump_t *)vepump;
    epcore_t   * pcore = NULL;
    ioevent_t  * ioe = NULL; 
    ioevent_t  * ioetmp = NULL; 
    int          num = 0;
 
    if (!epump) return -1;
 
    pcore = epump->epcore;
    if (!pcore) return -2;
 
    EnterCriticalSection(&epump->ioeventlistCS);
    ioe = lt_first(epump->ioevent_list);
    while (ioe) {
        ioetmp = lt_get_next(ioe);
        if (ioe->obj == obj) {
            lt_delete_ptr(epump->ioevent_list, ioe);
            bpool_recycle(pcore->event_pool, ioe);
            num++;
        }
        ioe = ioetmp;
    }
    LeaveCriticalSection(&epump->ioeventlistCS);
 
    return num;
}

void * ioevent_execute (void * vpcore, void * vioe)
{
    epcore_t   * pcore = (epcore_t *)vpcore;
    ioevent_t  * ioe = (ioevent_t *)vioe;

    iodev_t    * pdev = NULL;
    iotimer_t  * piot = NULL;
    GeneralCB  * gcb = NULL;
    IOHandler  * iocb = NULL;
#if defined(HAVE_SELECT) || defined(HAVE_IOCP)
    iodev_t    * ptmp = NULL;
    ulong        curid = 0;
#endif

    DnsMsg     * dnsmsg = NULL;

    if (!pcore) return NULL;
    if (!ioe) return NULL;

    if (ioe->externflag == 1) {
        gcb = (GeneralCB *)ioe->callback;
        if (!gcb) return NULL;
 
        (*gcb)(ioe->obj, ioe->ignresult);
        return NULL;
    }

    if (ioe->objid > 0 && 
        epcore_iodev_find(pcore, ioe->objid) != ioe->obj) {

        bpool_recycle(pcore->event_pool, ioe);
        ioe = NULL;

        return NULL;
    }

    switch (ioe->type) {
    case IOE_CONNECTED:
    case IOE_CONNFAIL:
    case IOE_ACCEPT:
    case IOE_READ:
    case IOE_WRITE:
    case IOE_INVALID_DEV:
        pdev = (iodev_t *)ioe->obj;
        if (!pdev || pdev->fd == INVALID_SOCKET)
            break;

        if (ioe->type == IOE_CONNECTED || 
            ioe->type == IOE_CONNFAIL ||
            ioe->type == IOE_ACCEPT)
            pdev->iostate = IOS_READWRITE;

#if defined(HAVE_SELECT) || defined(HAVE_IOCP)
        curid = pdev->id;
#endif

        if (pdev->callback)
            (*pdev->callback)(pdev->cbpara, pdev, ioe->type, pdev->fdtype);
        else if (pcore->callback)
            (*pcore->callback)(pcore->cbpara, pdev, ioe->type, pdev->fdtype);
 
#if defined(HAVE_SELECT) || defined(HAVE_IOCP)
        /* pdev may be closed during the execution of callback */

        ptmp = epcore_iodev_find(pcore, curid);

        if (ioe->type == IOE_INVALID_DEV && ptmp) {
            iodev_close(ptmp);
            ptmp = NULL;

        } else if (ptmp && ptmp->fd != INVALID_SOCKET) {
#ifdef HAVE_IOCP
            if (ptmp->fdtype == FDT_ACCEPTED || ptmp->fdtype == FDT_CONNECTED) {
                if (ptmp->rwflag & RWF_READ && ptmp->iostate == IOS_READWRITE) {
                    iocp_event_recv_post(ptmp, NULL, 0);
                }
                if (ptmp->rwflag & RWF_WRITE && ptmp->iostate == IOS_READWRITE) {
                    iocp_event_send_post(ptmp, NULL, 0, 0);
                }

            } else if (ptmp->fdtype == FDT_UDPSRV || ptmp->fdtype == FDT_UDPCLI) {
                if (ptmp->rwflag & RWF_READ && ptmp->iostate == IOS_READWRITE) {
                    iocp_event_recvfrom_post(ptmp, NULL, 0);
                }
            }
#else
            /* if underlying fd watching is epoll and ONESHOT mode,
               READ notify should be added each time after callback */
            iodev_set_poll(ptmp);
#endif
        }
#endif
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
 
    case IOE_DNS_RECV:
        dnsmsg = (DnsMsg *)ioe->obj;
        if (!dnsmsg) break;

        dns_msg_handle(dnsmsg);
        break;

    default:
        iocb = (IOHandler *)ioe->callback;
        if (iocb)
            (*iocb)(ioe->cbpara, ioe->obj, ioe->type, FDT_USERCMD);
        else if (pcore->callback)
            (*pcore->callback)(pcore->cbpara, ioe->obj, ioe->type, FDT_USERCMD);
        break;
    }
 
    /* recycle event to pool */
    bpool_recycle(pcore->event_pool, ioe);

    return NULL;
}


int ioevent_handle (void * vepump)
{
    epump_t    * epump = (epump_t *)vepump;
    epcore_t   * pcore = NULL;
    ioevent_t  * ioe = NULL;
    int          evnum = 0;

    if (!epump) return -1;

    pcore = (epcore_t *)epump->epcore;
    if (!pcore) return -2;

    while (!epump->quit) {
        ioe = ioevent_pop(epump);
        if (!ioe) break;

        epump->curioe = ioe;

        ioevent_execute(pcore, ioe);
        evnum++;

        epump->curioe = NULL;
    }

    return evnum;
}

void ioevent_print (void * vioe, char * title)
{
#ifdef _DEBUG
    ioevent_t  * ioe = (ioevent_t *)vioe;
    iodev_t    * pdev = NULL;
    DnsMsg     * dnsmsg = NULL;
    char         buf[256];

    if (!ioe) return;

    buf[0] = '\0';

    if (title) sprintf(buf+strlen(buf), "%s ObjID=%lu ", title, ioe->objid);

    if (ioe->type == IOE_CONNECTED)        sprintf(buf+strlen(buf), "IOE_CONNECTED");
    else if (ioe->type == IOE_CONNFAIL)    sprintf(buf+strlen(buf), "IOE_CONNFAIL");
    else if (ioe->type == IOE_ACCEPT)      sprintf(buf+strlen(buf), "IOE_ACCEPT");
    else if (ioe->type == IOE_READ)        sprintf(buf+strlen(buf), "IOE_READ");
    else if (ioe->type == IOE_WRITE)       sprintf(buf+strlen(buf), "IOE_WRITE");
    else if (ioe->type == IOE_TIMEOUT)     sprintf(buf+strlen(buf), "IOE_TIMEOUT");
    else if (ioe->type == IOE_DNS_RECV)    sprintf(buf+strlen(buf), "IOE_DNS_RECV");
    else if (ioe->type == IOE_INVALID_DEV) sprintf(buf+strlen(buf), "IOE_INVALID_DEV");
    else                                   sprintf(buf+strlen(buf), "Unknown");

    if (ioe->type != IOE_TIMEOUT && ioe->type != IOE_DNS_RECV) {
        sprintf(buf+strlen(buf), " ");
        pdev = (iodev_t *)ioe->obj;
        if (pdev->fdtype == FDT_LISTEN)               sprintf(buf+strlen(buf), "FDT_LISTEN");
        else if (pdev->fdtype == FDT_CONNECTED)       sprintf(buf+strlen(buf), "FDT_CONNECTED");
        else if (pdev->fdtype == FDT_ACCEPTED)        sprintf(buf+strlen(buf), "FDT_ACCEPTED");
        else if (pdev->fdtype == FDT_UDPSRV)          sprintf(buf+strlen(buf), "FDT_UDPSRV");
        else if (pdev->fdtype == FDT_UDPCLI)          sprintf(buf+strlen(buf), "FDT_UDPCLI");
        else if (pdev->fdtype == FDT_RAWSOCK)         sprintf(buf+strlen(buf), "FDT_RAWSOCK");
        else if (pdev->fdtype == FDT_TIMER)           sprintf(buf+strlen(buf), "FDT_TIMER");
        else if (pdev->fdtype == FDT_LINGER_CLOSE)    sprintf(buf+strlen(buf), "FDT_LINGER_CLOSE");
        else if (pdev->fdtype == FDT_STDIN)           sprintf(buf+strlen(buf), "FDT_STDIN");
        else if (pdev->fdtype == FDT_STDOUT)          sprintf(buf+strlen(buf), "FDT_STDOUT");
        else if (pdev->fdtype == FDT_USOCK_LISTEN)    sprintf(buf+strlen(buf), "FDT_USOCK_LISTEN");
        else if (pdev->fdtype == FDT_USOCK_CONNECTED) sprintf(buf+strlen(buf), "FDT_USOCK_CONNECTED");
        else if (pdev->fdtype == FDT_USOCK_ACCEPTED)  sprintf(buf+strlen(buf), "FDT_USOCK_ACCEPTED");
        else                                          sprintf(buf+strlen(buf), "Unknown Type");

        /*sprintf(buf+strlen(buf), " FD=%d WID=%lu R<%s:%d> L<%s:%d>",
                 pdev->fd, pdev->threadid, pdev->remote_ip, pdev->remote_port,
                 pdev->local_ip, pdev->local_port);*/
        sprintf(buf+strlen(buf), " FD=%d R<%s:%d> L<%s:%d>",
                 pdev->fd, pdev->remote_ip, pdev->remote_port,
                 pdev->local_ip, pdev->local_port);
    } else {
        if (ioe->obj && ioe->type == IOE_TIMEOUT) {
            sprintf(buf+strlen(buf), " CmdID=%d ID=%lu WID=%lu",
                    ((iotimer_t *)ioe->obj)->cmdid,
                    ((iotimer_t *)ioe->obj)->id,
                    ((iotimer_t *)ioe->obj)->threadid);

        } else if (ioe->obj && ioe->type == IOE_DNS_RECV) {
            dnsmsg = (DnsMsg *)ioe->obj;
            sprintf(buf+strlen(buf), " Name=%s MsgID=%u RCode=%d AnsNum=%d WID=%lu",
                    dnsmsg->name, dnsmsg->msgid, dnsmsg->rcode, dnsmsg->an_num, dnsmsg->threadid);
        }
    }
    printf("%s\n", buf);
#endif
}

