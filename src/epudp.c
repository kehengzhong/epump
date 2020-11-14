/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "tsock.h"

#include "epcore.h"
#include "iodev.h"
#include "mlisten.h"

 
void * epudp_listen_create (void * vpcore, char * localip, int port,
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
 
    pdev->fd = udp_listen(localip, port);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -200;
        return NULL;
    }
 
    sock_nonblock_set(pdev->fd, 1);
 
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

void * epudp_listen (void * vpcore, char * localip, int port, void * para, int * pret,
                     IOHandler * cb, void * cbpara, int bindtype)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
 
    if (pret) *pret = -1;
    if (!pcore) return NULL;
 
    if (bindtype == BIND_GIVEN_EPUMP)
        bindtype = BIND_ONE_EPUMP;
 
    if (bindtype != BIND_NONE      &&
        bindtype != BIND_ONE_EPUMP &&
        bindtype != BIND_ALL_EPUMP)
        return NULL;
 
    pdev = epudp_listen_create(pcore, localip, port, para, pret, cb, cbpara);
    if (!pdev) {
        return NULL;
    }
 
    /* bind one/more epump threads according to bindtype */
    iodev_bind_epump(pdev, bindtype, NULL);
 
    if (pret) *pret = 0;
 
    return pdev;
}
 
void * epudp_mlisten (void * vpcore, char * localip, int port, void * para,  
                      IOHandler * cb, void * cbpara)
{                            
    epcore_t * pcore = (epcore_t *)vpcore; 
    
    if (!pcore) return NULL;
 
    if (port <= 0 || port >= 65536) return NULL;
 
    return mlisten_open(pcore, localip, port, FDT_UDPSRV, para, cb, cbpara);
}



void * epudp_client (void * vpcore, char * localip, int port,
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
        pdev->fd = udp_listen(localip, port);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -200;
        return NULL;
    }
 
    sock_nonblock_set(pdev->fd, 1);
 
    pdev->local_port = port;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    pdev->iostate = IOS_READWRITE;
    pdev->fdtype = FDT_UDPCLI;
 
    iodev_rwflag_set(pdev, RWF_READ);
 
    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, BIND_ONE_EPUMP, NULL);
 
    if (retval) *retval = 0;
    return pdev;
}
 
