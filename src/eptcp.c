/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "tsock.h"
#include "strutil.h"
#include "arfifo.h"
#include "trace.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "ioevent.h"
#include "mlisten.h"
#include "epdns.h"

#ifdef HAVE_IOCP
#include "epiocp.h"
#endif


SOCKET tcp_listen_all (char * localip, int port, void * psockopt,
                        sockattr_t * fdlist, int * fdnum)
{
    struct addrinfo    hints;
    struct addrinfo  * result = NULL;
    struct addrinfo  * rp = NULL;
    SOCKET             aifd = INVALID_SOCKET;
    char               buf[128];
 
    SOCKET             listenfd = INVALID_SOCKET;
    sockopt_t        * sockopt = NULL;
    int                num = 0;
    int                rpnum = 0;
    int                one = 0;
    int                backlog = 511;
    int                ret = 0;
 
    sockopt = (sockopt_t *)psockopt;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;       /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;   /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;       /* For wildcard IP address */
    hints.ai_protocol = IPPROTO_TCP;   /* TCP protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
 
    sprintf(buf, "%d", port);
 
    if (localip && strlen(localip) <= 1)
        localip = NULL;
 
    aifd = getaddrinfo(localip, buf, &hints, &result);
    if (aifd != 0) {
        if (result) freeaddrinfo(result);
        tolog(1, "getaddrinfo: %s:%s return %d\n", localip, buf, aifd);
        return -100;
    }
 
    /* getaddrinfo() returns a list of address structures.
       Try each address until we successfully bind(2).
       If socket(2) (or bind(2)) fails, we (close the socket
       and) try the next address. */
 
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        rpnum++;
 
#ifdef HAVE_IOCP
        listenfd = WSASocket(rp->ai_family, rp->ai_socktype, rp->ai_protocol,
                             NULL, 0, WSA_FLAG_OVERLAPPED);
#else
        listenfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#endif
        if (listenfd == INVALID_SOCKET) {
            tolog(1, "tcp_listen_all socket() failed\n");
            continue;
        }
 
        if (sockopt) {
            if (sockopt->mask & SOM_BACKLOG)
                backlog = sockopt->backlog;
            sock_option_set(listenfd, sockopt);
 
        } else { //set the default options
            one = 1;
            ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int));
            if (ret != 0) perror("TCPListen REUSEADDR");
#ifdef SO_REUSEPORT
            ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (void *)&one, sizeof(int));
            if (ret != 0) perror("TCPListen REUSEPORT");
#endif
            ret = setsockopt(listenfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&one, sizeof(int));
            if (ret != 0) perror("TCPListen KEEPALIVE");
        }
 
        if (bind(listenfd, rp->ai_addr, rp->ai_addrlen) != 0) {
            memset(buf, 0, sizeof(buf));
            sock_addr_ntop(rp->ai_addr, buf);
            tolog(1, "tcp_listen_all bind %s:%d failed\n", buf, sock_addr_port(rp->ai_addr));
            closesocket(listenfd);
            listenfd = INVALID_SOCKET;
            continue;
        }
 
        if (backlog <= 0) backlog = 511;
        if (listen(listenfd, backlog) == SOCKET_ERROR) {
            memset(buf, 0, sizeof(buf));
            sock_addr_ntop(rp->ai_addr, buf);
            tolog(1, "tcp_listen_all fd=%d %s:%d failed\n", listenfd, buf, sock_addr_port(rp->ai_addr));
            closesocket(listenfd);
            listenfd = INVALID_SOCKET;
            continue;
        }
 
        if (fdlist && fdnum && num < *fdnum) {
            fdlist[num].fd = listenfd;
            fdlist[num].family = rp->ai_family;
            fdlist[num].socktype = rp->ai_socktype;
            fdlist[num].protocol = rp->ai_protocol;
            num++;
        } else
            break;
    }
    freeaddrinfo(result);
 
    if (fdnum) *fdnum = num;
 
    if (rpnum <= 0) {
        tolog(1, "tcp_listen_all no addrinfo available\n");
        /* there is no address/port that bound or listened successfully! */
        return INVALID_SOCKET;
    }
 
    return listenfd;
}


void * eptcp_listen_create (void * vpcore, char * localip, int port, void * para, int * retval,
                            IOHandler * cb, void * cbpara, iodev_t ** devlist, int * devnum)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
    sockopt_t   sockopt = {0};
    sockattr_t  fdlist[16];
    int         i, fdnum = 16;
    int         num = 0;
 
    if (retval) *retval = -1;
    if (!pcore) {
        if (devnum) *devnum = 0;
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

    tcp_listen_all(localip, port, &sockopt, fdlist, &fdnum);
    if (fdnum <= 0) {
        if (retval) *retval = -200;
        if (devnum) *devnum = 0;
        return NULL;
    }

    for (i = 0; i < fdnum; i++) {
        pdev = iodev_new(pcore);
        if (!pdev) break;

        pdev->fd = fdlist[i].fd;
        pdev->family = fdlist[i].family;
        pdev->socktype = fdlist[i].socktype;
        pdev->protocol = fdlist[i].protocol;

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

        if (devlist && devnum && num < *devnum)
            devlist[num++] = pdev;
        else
            break;
    }

    for ( ; i < fdnum; i++) {
        closesocket(fdlist[i].fd);
        fdlist[i].fd = INVALID_SOCKET;
    }

    if (devnum) *devnum = num;
    if (retval && num <= 0) *retval = -100;
    else if (retval) *retval = 0;

    if (pdev == NULL && num > 0 && devlist && devnum)
        pdev = devlist[0];

    return pdev;
}
 
void * eptcp_listen (void * vpcore, char * localip, int port, void * para, int * pret,
                     IOHandler * cb, void * cbpara, int bindtype, void ** plist, int * listnum)
{
    epcore_t * pcore = (epcore_t *)vpcore;
    iodev_t  * pdev = NULL;
    iodev_t  * devlist[16];
    int        devnum = 16;
    int        i = 0;
#ifdef HAVE_IOCP
    int        j = 0;
#endif
    int        num = 0;
 
    if (pret) *pret = -1;
    if (!pcore) return NULL;
 
    if (bindtype == BIND_GIVEN_EPUMP)
        bindtype = BIND_ONE_EPUMP;

    if (bindtype != BIND_NONE      && 
        bindtype != BIND_ONE_EPUMP && 
        bindtype != BIND_CURRENT_EPUMP &&
        bindtype != BIND_ALL_EPUMP)
        return NULL;

    pdev = eptcp_listen_create(pcore, localip, port, para, pret,
                               cb, cbpara, devlist, &devnum);
    if (devnum <= 0) {
        return NULL;
    }
 
    for (i = 0; i < devnum; i++) {
        /* bind one/more epump threads according to bindtype */
        iodev_bind_epump(devlist[i], bindtype, NULL);

        if (plist && listnum && i < *listnum)
            plist[num++] = devlist[i];

#ifdef HAVE_IOCP
        /* now Post 128 overlapped Accept packet to Completion Port
         * waiting for client connection */
        for (j = 0; j < 128; j++)
            iocp_event_accept_post(devlist[i]);
#endif
    }
 
    if (listnum) *listnum = num;
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
#ifndef HAVE_IOCP
    socklen_t   addrlen;
    SOCKET      clifd;
    ep_sockaddr_t  cliaddr;
    ep_sockaddr_t  sock;
#endif

    if (retval) *retval = -1;
    if (!pcore || !listendev) return NULL;
 
#ifdef HAVE_IOCP

    pdev = ar_fifo_out(listendev->devfifo);
    if (!pdev) {
        if (retval) *retval = -100;
        return NULL;
    }

#else

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
#if defined(_WIN32) || defined(_WIN64)
        shutdown(clifd, 0x02);//SD_RECEIVE=0x00, SD_SEND=0x01, SD_BOTH=0x02);
#endif
        closesocket(clifd);
        return NULL;
    }
 
    pdev->fd = clifd;
    pdev->family = cliaddr.u.addr.sa_family;
    pdev->socktype = listendev->socktype;
    pdev->protocol = listendev->protocol;

    sock_addr_ntop(&cliaddr, pdev->remote_ip);
    pdev->remote_port = sock_addr_port(&cliaddr);
 
    addrlen = sizeof(sock);
    if (getsockname(pdev->fd, (struct sockaddr *)&sock, (socklen_t *)&addrlen) == 0) {
        sock_addr_ntop(&sock, pdev->local_ip);
        pdev->local_port = sock_addr_port(&sock);
    }
    pdev->local_port = listendev->local_port;
 
#endif

    /* indicates the current worker thread will handle the upcoming read/write event */
    if (pcore->dispmode == 1)
        pdev->threadid = get_threadid();

    sock_nonblock_set(pdev->fd, 1);
 
    pdev->fdtype = FDT_ACCEPTED;
    pdev->iostate = IOS_READWRITE;

    pdev->para = para;
    pdev->callback = cb;
    pdev->cbpara = cbpara;
 
    iodev_rwflag_set(pdev, RWF_READ);

    if (retval) *retval = 0;

    /* epump is system-decided: select one lowest load epump thread to be bound */
    iodev_bind_epump(pdev, bindtype, NULL);

#ifdef HAVE_IOCP
    iocp_event_recv_post(pdev, NULL, 0);
#endif

    return pdev;
}
 
void * eptcp_connect (void * vpcore, char * host, int port,
                      char * localip, int localport, void * para,
                      int * retval, IOHandler * cb, void * cbpara)
{
    epcore_t  * pcore = (epcore_t *)vpcore;
    iodev_t   * pdev = NULL;
    int         succ = 0;
#ifndef HAVE_IOCP
    sockattr_t  attr;
#endif

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
 
#ifdef HAVE_IOCP

    iocp_event_connect_post(pdev, host, port, localip, localport, &succ);
    if (succ < 0) {
        iodev_close(pdev);
        if (retval) *retval = -30;
        return NULL;
    }

#else

    pdev->fd = tcp_nb_connect(host, port, localip, localport, &succ, &attr);
    if (pdev->fd == INVALID_SOCKET) {
        iodev_close(pdev);
        if (retval) *retval = -30;
        return NULL;
    }
 
    pdev->family = attr.family;
    pdev->socktype = attr.socktype;
    pdev->protocol = attr.protocol;

#endif

    if (succ > 0) { /* connect successfully */
        pdev->iostate = IOS_READWRITE;
        if (retval) *retval = 1;

        iodev_rwflag_set(pdev, RWF_READ);

    } else {
        pdev->iostate = IOS_CONNECTING;
        if (retval) *retval = -100;

#ifdef HAVE_IOCP
        iodev_rwflag_set(pdev, RWF_READ);
#else
        iodev_rwflag_set(pdev, RWF_READ | RWF_WRITE);
#endif
    }

    /* indicates the current worker thread will handle the upcoming read/write event */
    if (pcore->dispmode == 1)
        pdev->threadid = get_threadid();

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

    pdev->family = addr.family;
    pdev->socktype = addr.socktype;
    pdev->protocol = IPPROTO_TCP;

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

