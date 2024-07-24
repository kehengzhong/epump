/*
 * Copyright (c) 2003-2024 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 *
 * #####################################################
 * #                       _oo0oo_                     #
 * #                      o8888888o                    #
 * #                      88" . "88                    #
 * #                      (| -_- |)                    #
 * #                      0\  =  /0                    #
 * #                    ___/`---'\___                  #
 * #                  .' \\|     |// '.                #
 * #                 / \\|||  :  |||// \               #
 * #                / _||||| -:- |||||- \              #
 * #               |   | \\\  -  /// |   |             #
 * #               | \_|  ''\---/''  |_/ |             #
 * #               \  .-\__  '-'  ___/-. /             #
 * #             ___'. .'  /--.--\  `. .'___           #
 * #          ."" '<  `.___\_<|>_/___.'  >' "" .       #
 * #         | | :  `- \`.;`\ _ /`;.`/ -`  : | |       #
 * #         \  \ `_.   \_ __\ /__ _/   .-` /  /       #
 * #     =====`-.____`.___ \_____/___.-`___.-'=====    #
 * #                       `=---='                     #
 * #     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~   #
 * #               佛力加持      佛光普照              #
 * #  Buddha's power blessing, Buddha's light shining  #
 * #####################################################
 */

#include "btype.h"
#include "bpool.h"
#include "mthread.h"
#include "memory.h"
#include "trace.h"

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
    return 0;
}


int epump_ioevent_push (epump_t * epump, ioevent_t * ioe)
{
    if (!epump) return -1;
    if (!ioe) return -2;

    EnterCriticalSection(&epump->ioeventlistCS);
    lt_append(epump->ioevent_list, ioe);
    LeaveCriticalSection(&epump->ioeventlistCS);

#ifdef _DEBUG
{
    int  num;
    char title[128];

    num = lt_num(epump->ioevent_list);

    sprintf(title, "EPump: %lu RecvEvent[%d] ", epump->threadid, num);
    ioevent_print(ioe, title);
}
#endif

    return 0;
}

int ioevent_dispatch (void * vepump, void * vioe)
{
    epump_t    * epump = (epump_t *)vepump;
    worker_t   * wker = NULL;
    epump_t    * dstepump = NULL;
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
            tolog(1, "Panic: diapatch device event failed, type=%d ioe->obj=NULL\n", ioe->type);
            mpool_recycle(pcore->event_pool, ioe);
            return -100;
        }

        ioe->objid = pdev->id;
        threadid = pdev->threadid;
        dstepump = epump;
        break;

    /* When ListenDev accepts a connection request, we keep its threadid value at 0.
       It causes the worker thread with the lowest load to be assigned for event processing */
    case IOE_ACCEPT:
        pdev = (iodev_t *)ioe->obj;
        if (!pdev || pdev->fd == INVALID_SOCKET) {
            tolog(1, "Panic: diapatch device event IOE_ACCEPT failed, ioe->obj=NULL\n");
            mpool_recycle(pcore->event_pool, ioe);
            return -100;
        }

        ioe->objid = pdev->id;
        break;

    case IOE_TIMEOUT:
        piot = (iotimer_t *)ioe->obj;
        if (!piot) {
            tolog(1, "Panic: diapatch IOE_TIMER event failed, ioe->obj=NULL\n");
            mpool_recycle(pcore->event_pool, ioe);
            return -101;
        }

        ioe->objid = piot->id;
        threadid = piot->threadid;
        dstepump = epump;
        break;

    case IOE_DNS_RECV:
        dnsmsg = (DnsMsg *)ioe->obj;
        if (!dnsmsg) {
            tolog(1, "Panic: diapatch IOE_DNS_RECV event failed, ioe->obj=NULL\n");
            mpool_recycle(pcore->event_pool, ioe);
            return -101;
        }

        ioe->objid = dnsmsg->msgid;
        threadid = dnsmsg->threadid;
        break;

    case IOE_DNS_CLOSE:
        dnsmsg = (DnsMsg *)ioe->obj;
        if (!dnsmsg) {
            tolog(1, "Panic: diapatch IOE_DNS_CLOSE event failed, ioe->obj=NULL\n");
            mpool_recycle(pcore->event_pool, ioe);
            return -101;
        }

        ioe->objid = dnsmsg->msgid;
        threadid = dnsmsg->threadid;
        break;

    case IOE_USER_DEFINED:
        pdev = (iodev_t *)ioe->obj;
        if (pdev && epcore_iodev_find(pcore, pdev->id) == pdev) {
            ioe->objid = pdev->id;
            threadid = pdev->threadid;
        } else {
            threadid = get_threadid();
        }
        break;
    }

    if (ht_num(pcore->worker_tab) > 0) {
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
        }
    }

    if (ioe->type == IOE_ACCEPT) {
        if (!dstepump) dstepump = epump;

        return epump_ioevent_push(dstepump, ioe);
    }

    if (!dstepump) {
        if (threadid > 0 && threadid != epump->threadid)
            dstepump = epump_thread_find(pcore, threadid);

        if (!dstepump) dstepump = epump;
    }

    if (pdev) pdev->threadid = dstepump->threadid;
    if (piot) piot->threadid = dstepump->threadid;
    if (dnsmsg) dnsmsg->threadid = dstepump->threadid;

    return epump_ioevent_push(dstepump, ioe);
}


int ioevent_push (void * vepump, int event, void * obj, void * cb, void * cbpara)
{
    epump_t   * epump = (epump_t *)vepump;
    ioevent_t * ioe = NULL;

    if (!epump) return -1;

    ioe = (ioevent_t *)mpool_fetch(epump->epcore->event_pool);
    if (!ioe) {
        tolog(1, "Panic: ioevent_push ioe fetched failed, event=%d\n", event);
        return -10;
    }

    ioe->type = event;
    ioe->obj = obj;
    ioe->callback = cb;
    ioe->cbpara = cbpara;

    ioe->objid = 0;

    ioe->epumpid = epump->threadid;
    ioe->workerid = 0;

    epump->epcore->acc_event_num++;

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

            mpool_recycle(pcore->event_pool, ioe);
            //kfree(ioe);

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

    switch (ioe->type) {
    case IOE_CONNECTED:
    case IOE_CONNFAIL:
    case IOE_ACCEPT:
    case IOE_READ:
    case IOE_WRITE:
    case IOE_INVALID_DEV:
        if (ioe->objid > 0 && epcore_iodev_find(pcore, ioe->objid) != ioe->obj) {
            mpool_recycle(pcore->event_pool, ioe);
            return NULL;
        }

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
            iodev_close_by(pcore, curid);
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
        if (ioe->objid == 0 && piot) ioe->objid = piot->id;

        if (epcore_iotimer_find(pcore, ioe->objid) != ioe->obj) {
            mpool_recycle(pcore->event_pool, ioe);
            return NULL;
        }

        if (piot->cmdid == IOTCMD_IDLE) { //system inner timeout
            /* the devices in idle table have not been used for a long time,
             * and the system should clean them up. close the connection,
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

        iotimer_recycle(pcore, ioe->objid);
        break;
 
    case IOE_DNS_RECV:
        if (ioe->objid > 0 && dns_msg_mgmt_get(pcore->dnsmgmt, (uint16)ioe->objid) != ioe->obj) {
            mpool_recycle(pcore->event_pool, ioe);
            return NULL;
        }

        dnsmsg = (DnsMsg *)ioe->obj;
        if (!dnsmsg) break;

        dns_msg_handle(dnsmsg);
        break;

    case IOE_DNS_CLOSE:
        if (ioe->objid > 0 && dns_msg_mgmt_get(pcore->dnsmgmt, (uint16)ioe->objid) != ioe->obj) {
            mpool_recycle(pcore->event_pool, ioe);
            return NULL;
        }

        dnsmsg = (DnsMsg *)ioe->obj;
        if (!dnsmsg) break;

        dns_msg_close(dnsmsg);
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
    mpool_recycle(pcore->event_pool, ioe);

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
    else if (ioe->type == IOE_DNS_CLOSE) sprintf(buf+strlen(buf), "IOE_DNS_CLOSE");
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
        } else if (ioe->obj && ioe->type == IOE_DNS_CLOSE) {
            dnsmsg = (DnsMsg *)ioe->obj;
            sprintf(buf+strlen(buf), " Name=%s MsgID=%u RCode=%d AnsNum=%d WID=%lu",
                    dnsmsg->name, dnsmsg->msgid, dnsmsg->rcode, dnsmsg->an_num, dnsmsg->threadid);
        }
    }
    printf("%s\n", buf);
#endif
}

