/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "memory.h"
#include "dynarr.h"
#include "tsock.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "eptcp.h"
#include "epudp.h"
#include "mlisten.h"


void * mlisten_alloc (char * localip, int port, int fdtype, void * para, IOHandler * cb, void * cbpara)
{
    mlisten_t * mln = NULL;

    mln = kzalloc(sizeof(*mln));
    if (!mln) return NULL;

    if (localip)
        strncpy(mln->localip, localip, sizeof(mln->localip)-1);

    mln->port = port;
    mln->reuseport = 0;

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

    arr_pop_free(mln->devlist, iodev_close);

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

    arr_pop_free(pcore->glbmlisten_list, mlisten_free);
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
    epcore_t   * pcore = (epcore_t *)epcore;
    epump_t    * epump = (epump_t *)vepump;
    mlisten_t  * mln = 0;
    iodev_t    * pdev = NULL;
    int          i, num;
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
                pdev = eptcp_listen_create (pcore, mln->localip, mln->port, mln->para,
                                            &ret, mln->cb, mln->cbpara);
                if (pdev && pdev->reuseport)
                    mln->reuseport = 1;
            }
        } else if (mln->fdtype == FDT_UDPSRV) {
            if (mln->reuseport || pdev == NULL) {
                pdev = epudp_listen_create (pcore, mln->localip, mln->port, mln->para,
                                            &ret, mln->cb, mln->cbpara);
                if (pdev && pdev->reuseport)
                    mln->reuseport = 1;
            }
        }

        if (!pdev) continue;
 
        mlisten_iodev_add(mln, pdev);

        pdev->bindtype = BIND_NEW_FOR_EPUMP; //4
        pdev->epump = epump;
 
        epump_iodev_add(epump, pdev);
 
        if (epump->setpoll)
            (*epump->setpoll)(epump, pdev);
    }
 
    LeaveCriticalSection(&pcore->glbmlistenlistCS);
 
    return 0;
}
 
 
void * mlisten_open (void * epcore,  char * localip, int port, int fdtype,
                     void * para, IOHandler * cb, void * cbpara)
{
    epcore_t   * pcore = (epcore_t *)epcore;
    mlisten_t  * mln = NULL;
    iodev_t    * pdev = NULL;
    int          ret = 0;
    int          i, num;
    epump_t    * epump = NULL;
 
    if (!pcore) return NULL;
    if (port <= 0 || port >= 65536) return NULL;
 
    mln = epcore_mlisten_get(pcore, localip, port, fdtype);
    if (!mln) {
        mln = mlisten_alloc(localip, port, fdtype, para, cb, cbpara);
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
                pdev = eptcp_listen_create (pcore, mln->localip, mln->port, mln->para,
                                            &ret, mln->cb, mln->cbpara);

                if (pdev && pdev->reuseport)
                    mln->reuseport = 1;
            }

        } else if (mln->fdtype == FDT_UDPSRV) {
            if (mln->reuseport || pdev == NULL) {
                pdev = epudp_listen_create (pcore, mln->localip, mln->port, mln->para,
                                            &ret, mln->cb, mln->cbpara);

                if (pdev && pdev->reuseport)
                    mln->reuseport = 1;
            }
        }
        if (!pdev) continue;
 
        mlisten_iodev_add(mln, pdev);

        pdev->bindtype = BIND_NEW_FOR_EPUMP; //4
        pdev->epump = epump;
 
        epump_iodev_add(epump, pdev);
        (*epump->setpoll)(epump, pdev);
    }
 
    LeaveCriticalSection(&pcore->epumplistCS);
 
    return mln;
}

int mlisten_close (void * vmln)
{
    mlisten_t * mln = (mlisten_t *)vmln;
    epcore_t  * pcore = NULL;

    if (!mln) return -1;

    pcore = (epcore_t *)mln->pcore;
    if (!pcore) return -2;

    if (epcore_mlisten_del(pcore, mln) == NULL)
        return 0;

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


