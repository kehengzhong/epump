/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#include "btype.h"
#include "tsock.h"
#include "frame.h"

#include "epcore.h"
#include "iodev.h"
#include "mlisten.h"
#include "epump_local.h"

#ifdef HAVE_IOCP
#include "epiocp.h"
#endif


SOCKET udp_listen_all (char * localip, int port, void * psockopt,
                       sockattr_t * fdlist, int * fdnum)
{
    struct addrinfo    hints;
    struct addrinfo  * result = NULL;
    struct addrinfo  * rp = NULL;
    sockopt_t        * sockopt = NULL;
    SOCKET             aifd = INVALID_SOCKET;
    char               buf[128];
    SOCKET             listenfd = INVALID_SOCKET;
    int                num = 0;
    int                rpnum = 0;
    int                one, ret;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;       /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM;   /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;       /* For wildcard IP address */
    hints.ai_protocol = IPPROTO_UDP;   /* TCP protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
 
    if (localip && strlen(localip) <= 1)
        localip = NULL;
 
    sprintf(buf, "%d", port);
 
    sockopt = (sockopt_t *)psockopt;
 
    aifd = getaddrinfo(localip, buf, &hints, &result);
    if (aifd != 0) {
        if (result) freeaddrinfo(result);
        if (fdnum) *fdnum = 0;
        return -100;
    }
 
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        rpnum++;
 
#ifdef HAVE_IOCP
        listenfd = WSASocket(rp->ai_family, rp->ai_socktype, rp->ai_protocol,
                             NULL, 0, WSA_FLAG_OVERLAPPED);
#else
        listenfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
#endif
        if (listenfd == INVALID_SOCKET)
            continue;
 
        if (sockopt) {
            sock_option_set(listenfd, sockopt);
 
        } else { //set the default options
            one = 1;
            ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void *)&one, sizeof(int));
            if (ret != 0) perror("UDPListen REUSEADDR");
#ifdef SO_REUSEPORT
            ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, (void *)&one, sizeof(int));
            if (ret != 0) perror("UDPListen REUSEPORT");
#endif
            ret = setsockopt(listenfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&one, sizeof(int));
            if (ret != 0) perror("UDPListen KEEPALIVE");
        }
 
        if (bind(listenfd, rp->ai_addr, rp->ai_addrlen) != 0) {
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
        /* there is no address/port that bound or listened successfully! */
        return INVALID_SOCKET;
    }
 
    return listenfd;
}

 
void * epudp_listen_create (void * vpcore, char * localip, int port, void * para,
                            int * retval, IOHandler * cb, void * cbpara,
                            iodev_t ** devlist, int * devnum)
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
    sockopt.mask |= SOM_REUSEADDR;
    sockopt.reuseaddr = 1;
 
    sockopt.mask |= SOM_REUSEPORT;
    sockopt.reuseport = 1;
 
    sockopt.mask |= SOM_KEEPALIVE;
    sockopt.keepalive = 1;

    udp_listen_all(localip, port, &sockopt, fdlist, &fdnum);
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
 
        pdev->iostate = IOS_READWRITE;
        pdev->fdtype = FDT_UDPSRV;
 
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

void * epudp_listen (void * vpcore, char * localip, int port, void * para, int * pret,
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
 
    pdev = epudp_listen_create(pcore, localip, port, para, pret,
                               cb, cbpara, devlist, &devnum);
    if (devnum <= 0) {
        if (listnum) *listnum = 0;
        return NULL;
    }
 
    for (i = 0; i < devnum; i++) {
        /* bind one/more epump threads according to bindtype */
        iodev_bind_epump(devlist[i], bindtype, NULL);
 
        if (plist && listnum && i < *listnum)
            plist[num++] = devlist[i];
 
#ifdef HAVE_IOCP
        /* now Post 128 overlapped WSARecvFrom packet to Completion Port
         * waiting for client datagram coming */
        for (j = 0; j < 1; j++)
            iocp_event_recvfrom_post(devlist[i], NULL, 0);
#endif
    }
 
    if (listnum) *listnum = num;
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
                     void * para, int * retval, IOHandler * cb, void * cbpara,
                     iodev_t ** devlist, int * devnum)
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
    sockopt.mask |= SOM_REUSEADDR;
    sockopt.reuseaddr = 1;
 
    sockopt.mask |= SOM_REUSEPORT;
    sockopt.reuseport = 1;
 
    sockopt.mask |= SOM_KEEPALIVE;
    sockopt.keepalive = 1;

    udp_listen_all(localip, port, &sockopt, fdlist, &fdnum);
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
 
        pdev->iostate = IOS_READWRITE;
        pdev->fdtype = FDT_UDPCLI;
 
        iodev_rwflag_set(pdev, RWF_READ);
 
        /* epump is system-decided: select one lowest load epump thread to be bound */
        iodev_bind_epump(pdev, BIND_ONE_EPUMP, NULL);

#ifdef HAVE_IOCP
        iocp_event_recvfrom_post(pdev, NULL, 0);
#endif

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

int epudp_recvfrom (void * vdev, void * vfrm, void * addr, int * pnum)
{
    iodev_t       * pdev = (iodev_t *)vdev;
    frame_p         frm = (frame_p)vfrm;
#ifndef HAVE_IOCP
    ep_sockaddr_t   sock;
    int             socklen = 0;
#endif
    int             toread = 0;
    int             ret = 0;

    if (pnum) *pnum = 0;

    if (!pdev) return -1;
    if (!frm) return -2;

#ifdef HAVE_IOCP
    ret = toread = frameL(pdev->rcvfrm);
    if (ret <= 0) return ret;

    if (frame_rest(frm) < toread)
        frame_grow(frm, toread);

    frame_put_nlast(frm, frameP(pdev->rcvfrm), frameL(pdev->rcvfrm));

    sock_addr_to_epaddr(&pdev->sock, (ep_sockaddr_t *)addr);

    frame_empty(pdev->rcvfrm);
#else
    toread = sock_unread_data(iodev_fd(pdev));
    if (toread <= 0) toread = 8192;

    if (frame_rest(frm) < toread)
        frame_grow(frm, toread);

    socklen = sizeof(sock);
    memset(&sock, 0, sizeof(sock));

    ret = recvfrom(iodev_fd(pdev), frame_end(frm), toread, 0,
                   (struct sockaddr *)&sock, (socklen_t *)&socklen);
    if (ret >= 0) {
        sock_addr_to_epaddr(&sock, (ep_sockaddr_t *)addr);
        frame_len_add(frm, ret);
    }
#endif

    if (pnum && ret >= 0) *pnum = ret;

    return ret;
}

