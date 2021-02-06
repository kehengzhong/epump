/*
 * Copyright (c) 2003-2020 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "tsock.h"
#include "strutil.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "ioevent.h"
#include "mlisten.h"
#include "epdns.h"


void * eptcp_listen_create (void * vpcore, char * localip, int port, void * para, int * retval,
                            IOHandler * cb, void * cbpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    sockopt_t  sockopt = {0};
 
    if (retval) *retval = -1;
    if (!pcore) return NULL;
 
    pdev = iodev_new(pcore);
    if (!pdev) {
        if (retval) *retval = -100;
        return NULL;
    }
 
    memset(&sockopt, 0, sizeof(sockopt));
    sockopt.mask |= SOM_BACKLOG;
    sockopt.backlog = 511;

    sockopt.mask |= SOM_REUSEADDR;
    sockopt.reuseaddr = 1;

    sockopt.mask |= SOM_REUSEPORT;
    sockopt.reuseport = 1;

    sockopt.mask |= SOM_KEEPALIVE;
    sockopt.keepalive = 1;

    pdev->fd = tcp_listen(localip, port, &sockopt);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -200;
        return NULL;
    }
 
    pdev->reuseaddr = (sockopt.reuseaddr_ret == 0) ? 1 : 0;
    pdev->reuseport = (sockopt.reuseport_ret == 0) ? 1 : 0;
    pdev->keepalive = (sockopt.keepalive_ret == 0) ? 1 : 0;

    sock_nonblock_set(pdev->fd, 1);
 
    if (localip)
        strncpy(pdev->local_ip, localip, sizeof(pdev->local_ip)-1);
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
 
void * eptcp_listen (void * vpcore, char * localip, int port, void * para, int * pret,
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

    pdev = eptcp_listen_create(pcore, localip, port, para, pret, cb, cbpara);
    if (!pdev) {
        return NULL;
    }
 
    /* bind one/more epump threads according to bindtype */
    iodev_bind_epump(pdev, bindtype, NULL);
 
    if (pret) *pret = 0;

    return pdev;
}
 
void * eptcp_mlisten (void * vpcore, char * localip, int port, void * para,
                      IOHandler * cb, void * cbpara)
{
    epcore_t * pcore = (epcore_t *)vpcore;
 
    if (!pcore) return NULL;

    if (port <= 0 || port >= 65536) return NULL;

    return mlisten_open(pcore, localip, port, FDT_LISTEN, para, cb, cbpara);
}
 
 
void * eptcp_accept (void * vpcore, void * vld, void * para, int * retval,
                     IOHandler * cb, void * cbpara, int bindtype)
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
 
    /* indicates the current worker thread will handle the upcoming read/write event */
    if (pcore->dispmode == 1)
        pdev->threadid = get_threadid();

    pdev->fd = clifd;
    sock_addr_ntop(&cliaddr, pdev->remote_ip);
    pdev->remote_port = sock_addr_port(&cliaddr);
 
    sock_nonblock_set(pdev->fd, 1);
 
    addrlen = sizeof(sock);
    if (getsockname(pdev->fd, (struct sockaddr *)&sock, (socklen_t *)&addrlen) == 0) {
        sock_addr_ntop(&sock, pdev->local_ip);
        pdev->local_port = sock_addr_port(&sock);
    }
    pdev->local_port = listendev->local_port;
 
    pdev->fdtype = FDT_ACCEPTED;
    pdev->iostate = IOS_READWRITE;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    iodev_rwflag_set(pdev, RWF_READ);

    if (retval) *retval = 0;

    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, bindtype, NULL);

    return pdev;
}
 
void * eptcp_connect (void * vpcore, char * host, int port,
                      char * localip, int localport, void * para,
                      int * retval, IOHandler * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
    int         succ = 0;
 
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
 
    pdev->fd = tcp_nb_connect(host, port, localip, localport, &succ);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -30;
        return NULL;
    }
 
    /* indicates the current worker thread will handle the upcoming read/write event */
    if (pcore->dispmode == 1)
        pdev->threadid = get_threadid();

    if (succ > 0) { /* connect successfully */
        pdev->iostate = IOS_READWRITE;
        if (retval) *retval = 0;

        iodev_rwflag_set(pdev, RWF_READ);

    } else {
        pdev->iostate = IOS_CONNECTING;
        if (retval) *retval = -100;

        iodev_rwflag_set(pdev, RWF_READ | RWF_WRITE);
    }

    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, BIND_CURRENT_EPUMP, NULL);

    return pdev;
}
 

int eptcp_connect_dnscb (void * vdev, char * name, int len, void * cache, int status)
{
    iodev_t       * pdev = (iodev_t *)vdev;
    ep_sockaddr_t   addr;
    char            dstip[41];
    int             succ = 0;

    if (!pdev) return -1;

    if (status == DNS_ERR_IPV4 || status == DNS_ERR_IPV6) {
        str_secpy(dstip, sizeof(dstip)-1, name, len);

    } else if (dns_cache_getip(cache, 0, dstip, sizeof(dstip)-1) <= 0) {
        if (pdev->callback)
            (*pdev->callback)(pdev->cbpara, pdev, IOE_CONNFAIL, pdev->fdtype);
        return 0;
    }

    str_secpy(pdev->remote_ip, sizeof(pdev->remote_ip), dstip, strlen(dstip));

    if (sock_addr_parse(dstip, -1, pdev->remote_port, &addr) < 0) {
        if (pdev->callback)
            (*pdev->callback)(pdev->cbpara, pdev, IOE_CONNFAIL, pdev->fdtype);
        return 0;
    }

    pdev->fd = tcp_ep_connect(&addr, 1, pdev->local_ip, pdev->local_port, NULL, &succ);
    if (pdev->fd == INVALID_SOCKET) {
        if (pdev->callback)
            (*pdev->callback)(pdev->cbpara, pdev, IOE_CONNFAIL, pdev->fdtype);
        return 0;
    }

    if (succ > 0) { /* connect successfully */
        pdev->iostate = IOS_READWRITE;
        iodev_rwflag_set(pdev, RWF_READ);

        if (pdev->callback)
            (*pdev->callback)(pdev->cbpara, pdev, IOE_CONNECTED, pdev->fdtype);

    } else {
        pdev->iostate = IOS_CONNECTING;
        iodev_rwflag_set(pdev, RWF_READ | RWF_WRITE);
    }

    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, BIND_CURRENT_EPUMP, NULL);

    return 0;
}

void * eptcp_nb_connect (void * vpcore, char * host, int port,
                         char * localip, int localport, void * para,
                         int * retval, IOHandler * cb, void * cbpara)
{
    epcore_t      * pcore = (epcore_t *)vpcore;
    iodev_t       * pdev = NULL;
    ep_sockaddr_t   addr;
    int             succ = 0;

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

    str_secpy(pdev->local_ip, sizeof(pdev->local_ip), localip, strlen(localip));
    pdev->local_port = localport;

    pdev->remote_port = port;

    /* indicates the current worker thread will handle the upcoming read/write event */
    if (pcore->dispmode == 1)
        pdev->threadid = get_threadid();

    if (sock_addr_parse(host, -1, port, &addr) <= 0) {
        if (dns_nb_query(pcore->dnsmgmt, host, -1, NULL, NULL, eptcp_connect_dnscb, pdev) < 0) {
            iodev_close(pdev);
            if (retval) *retval = -30;
            return NULL;
        }

        pdev->iostate = IOS_RESOLVING;
        if (retval) *retval = -101;

        return pdev;
    }

    str_secpy(pdev->remote_ip, sizeof(pdev->remote_ip), host, strlen(host));

    pdev->fd = tcp_ep_connect(&addr, 1, localip, localport, NULL, &succ);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -30;
        return NULL;
    }

    if (succ > 0) { /* connect successfully */
        pdev->iostate = IOS_READWRITE;
        if (retval) *retval = 0;

        iodev_rwflag_set(pdev, RWF_READ);

    } else {
        pdev->iostate = IOS_CONNECTING;
        if (retval) *retval = -100;

        iodev_rwflag_set(pdev, RWF_READ | RWF_WRITE);
    }

    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, BIND_CURRENT_EPUMP, NULL);

    return pdev;
}

