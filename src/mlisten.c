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
#include "memory.h"
#include "dynarr.h"
#include "tsock.h"
#include "trace.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "eptcp.h"
#include "epudp.h"
#include "mlisten.h"

#ifdef HAVE_IOCP
#include "epiocp.h"
#endif


void * mlisten_alloc (char * localip, int port, int fdtype, void * popt, void * para, IOHandler * cb, void * cbpara)
{
    mlisten_t * mln = NULL;

    mln = kzalloc(sizeof(*mln));
    if (!mln) return NULL;

    if (localip)
        strncpy(mln->localip, localip, sizeof(mln->localip)-1);

    mln->port = port;
    mln->reuseport = 0;

    mln->popt = popt;
    mln->para = para;
    mln->cb = cb;
    mln->cbpara = cbpara;

    mln->fdtype = fdtype;
    mln->devlist = arr_new(4);

    return mln;
}

void mlisten_free (void * vmln)
{
    mlisten_t * mln= (mlisten_t *)vmln;

    if (!mln) return;

    if (mln->devlist) {
        arr_pop_free(mln->devlist, iodev_closeit);
        mln->devlist = NULL;
    }

    kfree(mln);
}

int mlisten_iodev_add (void * vmln, void * pdev)
{
    mlisten_t * mln= (mlisten_t *)vmln;
    int         i, num;

    if (!mln) return -1;
    if (!pdev) return -2;

    num = arr_num(mln->devlist);
    for (i = 0; i < num; i++) {
        if (pdev == arr_value(mln->devlist, i))
            return 0;
    }

    return arr_push(mln->devlist, pdev);
}

int epcore_mlisten_init (void * epcore)
{
    epcore_t * pcore = (epcore_t *)epcore;

    if (!pcore) return -1;

    InitializeCriticalSection(&pcore->glbmlistenlistCS);
    pcore->glbmlisten_list = arr_new(4);

    return 0;
}

int epcore_mlisten_clean (void * epcore)
{
    epcore_t * pcore = (epcore_t *)epcore;

    if (!pcore) return -1;

    while (arr_num(pcore->glbmlisten_list) > 0)
        mlisten_close(arr_pop(pcore->glbmlisten_list));
    arr_free(pcore->glbmlisten_list);

    pcore->glbmlisten_list = NULL;

    DeleteCriticalSection(&pcore->glbmlistenlistCS);

    return 0;
}

 
int epcore_mlisten_add (void * epcore, void * vmln)
{
    epcore_t  * pcore = (epcore_t *)epcore;
    mlisten_t * mln= (mlisten_t *)vmln;

    if (!pcore) return -1;
    if (!mln) return -2;

    EnterCriticalSection(&pcore->glbmlistenlistCS);
    mln->pcore = pcore;
    arr_push(pcore->glbmlisten_list, mln);
    LeaveCriticalSection(&pcore->glbmlistenlistCS);

    return 0;
}

void * epcore_mlisten_get (void * epcore, char * localip, int port, int fdtype)
{
    epcore_t  * pcore = (epcore_t *)epcore;
    mlisten_t * mln = NULL;
    int         i, num;
 
    if (!pcore) return NULL;
 
    if (localip == NULL) localip = "";
 
    EnterCriticalSection(&pcore->glbmlistenlistCS);

    num = arr_num(pcore->glbmlisten_list);
    for (i = 0; i < num; i++) {
        mln = arr_value(pcore->glbmlisten_list, i);

        if (mln && mln->port == port && strcasecmp(mln->localip, localip) == 0) {
            LeaveCriticalSection(&pcore->glbmlistenlistCS);

            return mln;
        }
    }

    LeaveCriticalSection(&pcore->glbmlistenlistCS);
 
    return NULL;
}

void * epcore_mlisten_del (void * epcore, void * vmln)
{
    epcore_t  * pcore = (epcore_t *)epcore;
    mlisten_t * mln = (mlisten_t *)vmln;
    mlisten_t * iter = NULL;
    int         i, num;
    int         ret = 0;
 
    if (!pcore) return NULL;
    if (!mln) return NULL;
 
    EnterCriticalSection(&pcore->glbmlistenlistCS);

    num = arr_num(pcore->glbmlisten_list);
    for (i = 0; i < num; i++) {
        iter = arr_value(pcore->glbmlisten_list, i);
        if (!iter || iter == mln) {
            arr_delete(pcore->glbmlisten_list, i);
            i--; num--;
            if (iter) ret++;
            continue;
        }
    }
    LeaveCriticalSection(&pcore->glbmlistenlistCS);
 
    if (ret) return mln;

    return NULL;
}

 
int epcore_mlisten_create (void * epcore, void * vepump)
{
#ifdef HAVE_IOCP
    return 0;
#else
    epcore_t   * pcore = (epcore_t *)epcore;
    epump_t    * epump = (epump_t *)vepump;
    mlisten_t  * mln = 0;
    iodev_t    * pdev = NULL;
    iodev_t    * devlist[16];
    int          devnum = 16;
    int          iter = 0;
    int          i, num, mlndevs = 0;
    int          ret = 0;
 
    if (!pcore) return -1;
    if (!epump) return -2;
 
    EnterCriticalSection(&pcore->glbmlistenlistCS);
 
    num = arr_num(pcore->glbmlisten_list);
    for (i = 0; i < num; i++) {
 
        mln = arr_value(pcore->glbmlisten_list, i);
        if (!mln || mln->port <= 0 || mln->port >= 65535)
            continue;
 
        /* if DO NOT support REUSEPORT, get one existing iodev to bind */
        if (mln->reuseport == 0) {
            pdev = arr_value(mln->devlist, 0);
        } else {
            pdev = NULL;
        }

        if (mln->fdtype == FDT_LISTEN) {
            if (mln->reuseport || pdev == NULL) {
                mlndevs = arr_num(mln->devlist);
                devnum = 16;
                eptcp_listen_create (pcore, mln->localip, mln->port, mln->popt, mln->para,
                                     mln->cb, mln->cbpara, devlist, &devnum, &ret);
                for (iter = 0; iter < devnum; iter++) {
                    pdev = devlist[iter];
                    if (pdev && pdev->reuseport)
                        mln->reuseport = 1;
 
                    mlisten_iodev_add(mln, pdev);
                }
            }
        } else if (mln->fdtype == FDT_UDPSRV) {
            if (mln->reuseport || pdev == NULL) {
                mlndevs = arr_num(mln->devlist);
                devnum = 16;
                epudp_listen_create (pcore, mln->localip, mln->port, mln->popt, mln->para,
                                     mln->cb, mln->cbpara, devlist, &devnum, &ret);
                for (iter = 0; iter < devnum; iter++) {
                    pdev = devlist[iter];
                    if (pdev && pdev->reuseport)
                        mln->reuseport = 1;
 
                    mlisten_iodev_add(mln, pdev);
                }
            }
        }

        for (iter = mlndevs; iter < arr_num(mln->devlist); iter++) {
            pdev = arr_value(mln->devlist, iter);
            if (!pdev) continue;

            pdev->bindtype = BIND_NEW_FOR_EPUMP; //4
            pdev->epump = epump;
 
            epump_iodev_add(epump, pdev);
 
            if (epump->setpoll) {
                ret = -12;
                ret = (*epump->setpoll)(epump, pdev);
            }

            tolog(1, "glbMListen[%d/%d]: mln[%s:%d Reuse:%d fdtype:%d] Dev[%d/%d/%d: "
                     "%s %d %lu/%d bindtype=%d thid=%lu] "
                     "ePump[eplfd:%d %lu DevN:%d TimerN=%d ioeN=%d] ret:%d\n",
                  i, num, mln->localip, mln->port, mln->reuseport, mln->fdtype, iter,
                  arr_num(mln->devlist), devnum, pdev->local_ip, pdev->local_port,
                  pdev->id, pdev->fd, pdev->bindtype, pdev->threadid,
                  epump->epoll_fd, epump->threadid, rbtree_num(epump->device_tree),
                  rbtree_num(epump->timer_tree), lt_num(epump->ioevent_list), ret);
        }
    }
 
    LeaveCriticalSection(&pcore->glbmlistenlistCS);
 
    return 0;
#endif
}
 
 
void * mlisten_open (void * epcore,  char * localip, int port, int fdtype,
                     void * popt, void * para, IOHandler * cb, void * cbpara)
{
#ifdef HAVE_IOCP
    epcore_t   * pcore = (epcore_t *)epcore;
    mlisten_t  * mln = NULL;
    iodev_t    * pdev = NULL;
    iodev_t    * devlist[16];
    int          devnum = 16;
    int          i, j;
    int          ret = 0;

    if (!pcore) return NULL;
    if (port <= 0 || port >= 65536) return NULL;
 
    mln = epcore_mlisten_get(pcore, localip, port, fdtype);
    if (!mln) {
        mln = mlisten_alloc(localip, port, fdtype, popt, para, cb, cbpara);
        if (mln)
            epcore_mlisten_add(pcore, mln);
    }
    if (!mln) return NULL;

    devnum = 16; 
    if (mln->fdtype == FDT_LISTEN) {
        eptcp_listen_create (pcore, mln->localip, mln->port, mln->popt, mln->para,
                             mln->cb, mln->cbpara, devlist, &devnum, &ret);
    } else if (mln->fdtype == FDT_UDPSRV) {
        epudp_listen_create (pcore, mln->localip, mln->port, mln->popt, mln->para,
                             mln->cb, mln->cbpara, devlist, &devnum, &ret);
    } else {
        return NULL;
    }

    for (i = 0; i < devnum; i++) { 
        pdev = devlist[i]; 
        if (!pdev) continue;

        if (pdev->reuseport)
            mln->reuseport = 1; 

        mlisten_iodev_add(mln, pdev);

        pdev->bindtype = BIND_NEW_FOR_EPUMP; //4

        epump_iocp_setpoll(NULL, pdev);

        if (mln->fdtype == FDT_LISTEN) {
            /* now Post 128 overlapped Accept packet to Completion Port
             * waiting for client connection */
            for (j = 0; j < 128; j++) {
                iocp_event_accept_post(pdev);
            }
        } else if (mln->fdtype == FDT_UDPSRV) {
            /* now Post 128 overlapped WSARecvFrom packet to Completion Port
             * waiting for client datagram coming */
            for (j = 0; j < 1; j++) {
                iocp_event_recvfrom_post(pdev, NULL, 0);
            }
        }
    }

    return mln;

#else

    epcore_t   * pcore = (epcore_t *)epcore;
    mlisten_t  * mln = NULL;
    iodev_t    * pdev = NULL;
    iodev_t    * devlist[16];
    int          devnum = 16;
    int          iter = 0;
    int          ret = 0;
    int          i, num, mlndevs = 0;
    epump_t    * epump = NULL;
 
    if (!pcore) return NULL;
    if (port <= 0 || port >= 65536) return NULL;
 
    mln = epcore_mlisten_get(pcore, localip, port, fdtype);
    if (!mln) {
        mln = mlisten_alloc(localip, port, fdtype, popt, para, cb, cbpara);
        if (mln)
            epcore_mlisten_add(pcore, mln);
    }
    if (!mln) return NULL;
 
    /* if DO NOT support REUSEPORT, get one existing iodev to bind */
    if (mln->reuseport == 0) {
        pdev = arr_value(mln->devlist, 0);
    } else {
        pdev = NULL;
    }

    /* now create one listen socket for each running epump thread */
    EnterCriticalSection(&pcore->epumplistCS);
 
    num = arr_num(pcore->epump_list);
    for (i = 0; i < num; i++) {
 
        epump = arr_value(pcore->epump_list, i);
        if (!epump) continue;
 
        if (mln->fdtype == FDT_LISTEN) {
            if (mln->reuseport || pdev == NULL) {
                mlndevs = arr_num(mln->devlist);
                devnum = 16;
                eptcp_listen_create (pcore, mln->localip, mln->port, mln->popt, mln->para,
                                     mln->cb, mln->cbpara, devlist, &devnum, &ret);
                for (iter = 0; iter < devnum; iter++) {
                    pdev = devlist[iter];
                    if (!pdev) continue;

                    if (pdev->reuseport) mln->reuseport = 1;

                    mlisten_iodev_add(mln, pdev);
                }
            }

        } else if (mln->fdtype == FDT_UDPSRV) {
            if (mln->reuseport || pdev == NULL) {
                mlndevs = arr_num(mln->devlist);
                devnum = 16;
                epudp_listen_create (pcore, mln->localip, mln->port, mln->popt, mln->para,
                                     mln->cb, mln->cbpara, devlist, &devnum, &ret);
                for (iter = 0; iter < devnum; iter++) {
                    pdev = devlist[iter];
                    if (!pdev) continue;

                    if (pdev->reuseport) mln->reuseport = 1;

                    mlisten_iodev_add(mln, pdev);
                }
            }
        }

        for (iter = mlndevs; iter < arr_num(mln->devlist); iter++) {
            pdev = arr_value(mln->devlist, iter);
            if (!pdev) continue;

            pdev->bindtype = BIND_NEW_FOR_EPUMP; //4
            pdev->epump = epump;
 
            epump_iodev_add(epump, pdev);
            ret = (*epump->setpoll)(epump, pdev);

            tolog(1, "MListenOpen: id=%lu fd=%d fdtype=%d lport=%d bindtype=%d threadid=%lu "
                     "BindTo epump: eplfd=%d threadid=%lu devnum=%d timernum=%d ioenum=%d, ret=%d\n",
                  pdev->id, pdev->fd, pdev->fdtype, pdev->local_port, pdev->bindtype, pdev->threadid,
                  epump->epoll_fd, epump->threadid, rbtree_num(epump->device_tree),
                  rbtree_num(epump->timer_tree), lt_num(epump->ioevent_list), ret);
        }
    }
 
    LeaveCriticalSection(&pcore->epumplistCS);
 
    return mln;
#endif
}

int mlisten_close (void * vmln)
{
    mlisten_t * mln = (mlisten_t *)vmln;
    epcore_t  * pcore = NULL;
    int         i, num, iter;
    epump_t   * epump = NULL;
    iodev_t   * pdev = NULL;

    if (!mln) return -1;

    pcore = (epcore_t *)mln->pcore;
    if (!pcore) return -2;

    if (epcore_mlisten_del(pcore, mln) == NULL)
        return 0;

    /* unbind the devices from every epump */
    EnterCriticalSection(&pcore->epumplistCS);
 
    num = arr_num(pcore->epump_list);
    for (i = 0; i < num; i++) {
 
        epump = arr_value(pcore->epump_list, i);
        if (!epump) continue;
 
        for (iter = 0; iter < arr_num(mln->devlist); iter++) {
            pdev = arr_value(mln->devlist, iter);
            if (!pdev) continue;

            pdev->epump = NULL;

            if (epump_iodev_del(epump, pdev->fd) == NULL) {
                tolog(1, "Warning: MListenClose %llu/%d MLN[%s:%d reuse:%d %d] [%lu %d %s %d %d] DevNum:%d/%d\n",
                      epump->threadid, num, mln->localip, mln->port, mln->reuseport, arr_num(mln->devlist),
                      pdev->id, pdev->fd, pdev->remote_ip, pdev->fdtype, pdev->bindtype,
                      rbtree_num(epump->device_tree), ht_num(pcore->device_table));
            }

            (*epump->delpoll)(epump, pdev);
        }
    }

    LeaveCriticalSection(&pcore->epumplistCS);

    mlisten_free(mln);

    return 1;
}

int mlisten_port (void * vmln)
{
    mlisten_t * mln = (mlisten_t *)vmln;

    if (!mln) return -1;

    return mln->port;
}

char * mlisten_lip (void * vmln)
{
    mlisten_t * mln = (mlisten_t *)vmln;

    if (!mln) return "";

    return mln->localip;
}


