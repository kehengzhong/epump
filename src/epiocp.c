/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifdef HAVE_IOCP

#include "btype.h"
#include "memory.h"
#include "tsock.h"
#include "btime.h"
#include "mthread.h"
#include "hashtab.h"
#include "bpool.h"
#include "frame.h"
#include "chunk.h"
#include "trace.h"
#include "fileop.h"
#include "arfifo.h"
#include "service.h"

#include "epcore.h"
#include "epump_local.h"
#include "iodev.h"
#include "iotimer.h"
#include "ioevent.h"
#include "epwakeup.h"

#include <mswsock.h>
#include <stddef.h>

#include "epiocp.h"


#ifndef WSAID_ACCEPTEX
#define WSAID_ACCEPTEX {0xb5367df1,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif

#ifndef WSAID_CONNECTEX
#define WSAID_CONNECTEX {0x25a207b9,0xddf3,0x4660,{0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e}}
#endif

#ifndef WSAID_GETACCEPTEXSOCKADDRS
#define WSAID_GETACCEPTEXSOCKADDRS {0xb5367df2,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif

AcceptExPtr * fnAcceptEx = NULL;
ConnectExPtr * fnConnectEx = NULL;
GetAcceptExSockaddrsPtr * fnGetAcceptExSockaddrs = NULL;


void * iocp_event_alloc (int extsize)
{
    iocp_event_t * cpe = NULL;

    cpe = kzalloc(sizeof(*cpe) + extsize);

    cpe->fd = INVALID_SOCKET;
    cpe->clifd = INVALID_SOCKET;
    cpe->len = extsize;

    return cpe;
}

void iocp_event_free (void * vcpe)
{
    iocp_event_t * cpe = (iocp_event_t *)vcpe;

    if (!cpe) return;

    if (cpe->evtype == IOE_ACCEPT) {
        if (cpe->clifd != INVALID_SOCKET) {
            shutdown(cpe->clifd, 0x02);//SD_RECEIVE=0x00, SD_SEND=0x01, SD_BOTH=0x02);
            closesocket(cpe->clifd);
            cpe->clifd = INVALID_SOCKET;
        }
    }

    if (cpe->evtype == IOE_WRITE && cpe->mapsize > 0 && cpe->pmap) {
        UnmapViewOfFile(cpe->pmap);
        CloseHandle(cpe->hmap);
    }

    kfree(cpe);
}

void * iocp_event_accept_post (void * vdev)
{
    iodev_t      * pdev = (iodev_t *)vdev;
    iocp_event_t * cpe = NULL;
    DWORD          bytes = 0;
    BOOL           ret = 0;
    int            err = 0;
    int            len = 0;

    if (!pdev) return NULL;

    if (pdev->family == AF_INET)
        len = sizeof(struct sockaddr_in) + 16;
    else if (pdev->family == AF_INET6)
        len = sizeof(struct sockaddr_in6) + 16;
    else 
        return NULL;

    cpe = iocp_event_alloc(len * 2);
    if (!cpe) return NULL;

    cpe->evtype = IOE_ACCEPT;
    cpe->fd = pdev->fd;
    cpe->devid = pdev->id;
    cpe->state = IOCP_ACCEPT_IN_PROGRESS;

    cpe->clifd = WSASocket(pdev->family, pdev->socktype, pdev->protocol,
                           NULL, 0, WSA_FLAG_OVERLAPPED);

    ret = fnAcceptEx(pdev->fd, cpe->clifd, cpe->buf, 0, len, len, &bytes, &cpe->ovlap);
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            tolog(1, "iocp_event_accept_post: AcceptEx failed %d\n", err);
            closesocket(cpe->clifd);
            cpe->clifd = INVALID_SOCKET;
            iocp_event_free(cpe);
            return NULL;
        }
    }

    return cpe;
}

void * iocp_event_recv_post (void * vdev, void * pbuf, int len)
{
    iodev_t      * pdev = (iodev_t *)vdev;
    iocp_event_t * cpe = NULL;
    DWORD          recvbytes = 0;
    DWORD          flags = 0;
    int            ret = 0;
    int            err = 0;

    if (!pdev) return NULL;

    if (pdev->iocprecv > 1) return NULL;

    cpe = iocp_event_alloc(0);
    if (!cpe) return NULL;

    cpe->evtype = IOE_READ;
    cpe->fd = pdev->fd;
    cpe->devid = pdev->id;
    cpe->state = IOCP_READ_IN_PROGRESS;

    cpe->bufcnt = 1;
    if (pbuf && len > 0) {
        cpe->bufs[0].buf = pbuf;
        cpe->bufs[0].len = len;
    } else {
        cpe->bufs[0].buf = NULL;
        cpe->bufs[0].len = 0;
    }

    ret = WSARecv(pdev->fd, cpe->bufs, cpe->bufcnt, &recvbytes, &flags, &cpe->ovlap, NULL);
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            tolog(1, "iocp_event_recv_post: WSARecv failed %d\n", err);
            iocp_event_free(cpe);
            return NULL;
        }
    }

    pdev->iocprecv++;

    return cpe;
}


void * iocp_event_recvfrom_post (void * vdev, void * pbuf, int len)
{
    iodev_t      * pdev = (iodev_t *)vdev;
    iocp_event_t * cpe = NULL;
    int            bufsize = 16384;
    DWORD          recvbytes = 0;
    DWORD          flags = MSG_PARTIAL;
    int            ret = 0;
    int            err = 0;

    if (!pdev) return NULL;

    if (pbuf && len > 0) bufsize = 0;

    cpe = iocp_event_alloc(bufsize);
    if (!cpe) return NULL;

    cpe->evtype = IOE_READ;
    cpe->fd = pdev->fd;
    cpe->devid = pdev->id;
    cpe->state = IOCP_READ_IN_PROGRESS;

    cpe->bufcnt = 1;
    if (pbuf && len > 0) {
        cpe->bufs[0].buf = pbuf;
        cpe->bufs[0].len = len;
    } else {
        if (!pdev->rcvfrm) pdev->rcvfrm = frame_new(16384);
        frame_empty(pdev->rcvfrm);

        cpe->bufs[0].buf = frameP(pdev->rcvfrm);
        cpe->bufs[0].len = frame_size(pdev->rcvfrm);
    }

    pdev->socklen = sizeof(pdev->sock);
    cpe->flags = MSG_PARTIAL;

    ret = WSARecvFrom(pdev->fd, cpe->bufs, cpe->bufcnt, &recvbytes, &cpe->flags,
                      (struct sockaddr *)&pdev->sock, &pdev->socklen,
                      &cpe->ovlap, NULL);
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            tolog(1, "iocp_event_recvfrom_post: WSARecvFrom failed %d\n", err);
            iocp_event_free(cpe);
            return NULL;
        }
    }

    pdev->iocprecv++;

    return cpe;
}

void * iocp_event_send_post (void * vdev, void * chunk, int64 pos, int httpchunk)
{
    iodev_t      * pdev = (iodev_t *)vdev;
    iocp_event_t * cpe = NULL;
    DWORD          sendbytes = 0;
    DWORD          flags = 0;
    int            i, ret = 0;
    int            err = 0;
    chunk_vec_t    iovec;

    void         * pbyte = NULL;
    int64          maplen = 0;
    int64          mapoff = 0;

    if (!pdev) return NULL;

    if (!chunk && pdev->iocpsend > 1) return NULL;

    if (chunk) {
        memset(&iovec, 0, sizeof(iovec));
        ret = chunk_vec_get(chunk, pos, &iovec, httpchunk);
        if (ret < 0) return NULL;
    
        if (iovec.size == 0) {
            /* no available data to send */
            return NULL;
        }
    
        if (iovec.size > 0 && iovec.vectype != 1 && iovec.vectype != 2) {
            return NULL;
        }
    }

    cpe = iocp_event_alloc(0);
    if (!cpe) return NULL;

    cpe->evtype = IOE_WRITE;
    cpe->fd = pdev->fd;
     cpe->devid = pdev->id;
    cpe->state = IOCP_WRITE_IN_PROGRESS;

    if (chunk) {
        if (iovec.vectype == 1) { //memory buffer
            for (i = 0; i < IOCP_BUF_CNT && i < iovec.iovcnt; i++) {
                cpe->bufs[i].buf = iovec.iovs[i].iov_base;
                cpe->bufs[i].len = iovec.iovs[i].iov_len;
            }
            cpe->bufcnt = i;
    
        } else if (iovec.vectype == 2) { //sendfile
            cpe->mapsize = 8192 * 1024;
            if (iovec.size < cpe->mapsize)
                cpe->mapsize = iovec.size;
    
            pbyte = file_mmap(NULL, iovec.filehandle, iovec.fpos, cpe->mapsize,
                              NULL, &cpe->hmap, &cpe->pmap, &maplen, &mapoff);
            if (!pbyte) {
                tolog(1, "iocp_event_send_post: file mapping failed. offse=%I64d size=%I64d\n",
                      iovec.fpos, cpe->mapsize);
                iocp_event_free(cpe);
                return NULL;
            }
    
            cpe->bufs[0].buf = pbyte;
            cpe->bufs[0].len = cpe->mapsize;
            cpe->bufcnt = 1;
        }

    } else {
        cpe->bufs[0].buf = NULL;
        cpe->bufs[0].len = 0;
        cpe->bufcnt = 1;
    }

    ret = WSASend(pdev->fd, cpe->bufs, cpe->bufcnt, &sendbytes, flags, &cpe->ovlap, NULL);
    if (ret == SOCKET_ERROR) {
        err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            tolog(1, "iocp_event_send_post: WSASend failed %d\n", err);
            iocp_event_free(cpe);
            return NULL;
        }
    }

    pdev->iocpsend++;

    return cpe;
}

void * iocp_event_connect_post (void * vdev, char * host, int port, char * lip, int lport, int * retval)
{
    iodev_t          * pdev = (iodev_t *)vdev;
    iocp_event_t     * cpe = NULL;
    int                ret = 0;
 
    struct addrinfo    hints;
    struct addrinfo  * result = NULL;
    struct addrinfo  * rp = NULL;
    SOCKET             aifd = INVALID_SOCKET;
    char               buf[128];

    SOCKET             confd = INVALID_SOCKET;
    ep_sockaddr_t      addr;
    BOOL               rc = FALSE;

    if (retval) *retval = -1;

    if (!pdev) return NULL;
    if (!host || strlen(host) <= 0) return NULL;
    if (port <= 0) return NULL;
 
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;       /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;   /* Stream socket */
    hints.ai_flags = 0;               /* For wildcard IP address */
    hints.ai_protocol = IPPROTO_TCP;   /* TCP protocol */
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
 
    sprintf(buf, "%d", port);
 
    aifd = getaddrinfo(host, buf, &hints, &result);
    if (aifd != 0) {
        if (result) freeaddrinfo(result);
        tolog(1, "iocp_event_connect_post: getaddrinfo failed, %s:%s return %d\n", host, buf, aifd);
        if (retval) *retval = -10;
        return NULL;
    }

    cpe = iocp_event_alloc(0);
    if (!cpe) {
        if (result) freeaddrinfo(result);
        if (retval) *retval = -20;
        return NULL;
    }
 
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        //confd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        confd = WSASocket(rp->ai_family, rp->ai_socktype, rp->ai_protocol,
                          NULL, 0, WSA_FLAG_OVERLAPPED);
        if (confd == INVALID_SOCKET)
            continue;

        ret = 1;
        setsockopt(confd, SOL_SOCKET, SO_REUSEADDR, (void *)&ret, sizeof(int));
#ifdef SO_REUSEPORT
        setsockopt(confd, SOL_SOCKET, SO_REUSEPORT, (void *)&ret, sizeof(int));
#endif
        setsockopt(confd, SOL_SOCKET, SO_KEEPALIVE, (void *)&ret, sizeof(int));
 
        sock_nonblock_set(confd, 1);

        memset(&addr, 0, sizeof(addr));
        if (lip && strlen(lip) > 0 && lport > 0) {
            ret = sock_addr_acquire(&addr, lip, lport, SOCK_STREAM);
            if (ret <= 0 || bind(confd, (struct sockaddr *)&addr.u.addr, addr.socklen) != 0) {
                if (ret > 1) sock_addr_freenext(&addr);
                closesocket(confd);
                confd = -1;
                continue;
            }

        } else {
            if (rp->ai_family == AF_INET) {
                addr.family = AF_INET;
                addr.socklen = sizeof(addr.u.addr4);
                addr.u.addr4.sin_family = AF_INET;
                addr.u.addr4.sin_addr.s_addr = INADDR_ANY;
                addr.u.addr4.sin_port = htons((uint16)lport);

            } else if (rp->ai_family == AF_INET6) {
                addr.family = AF_INET6;
                addr.socklen = sizeof(addr.u.addr6);
                addr.u.addr6.sin6_family = AF_INET;
                addr.u.addr6.sin6_addr = in6addr_any;
                addr.u.addr6.sin6_port = htons((uint16)lport);

            } else {
                closesocket(confd);
                confd = -1;
                continue;
            }

            if (bind(confd, (struct sockaddr *)&addr.u.addr, addr.socklen) != 0) {
                closesocket(confd);
                confd = -1;
                continue;
            }
        }

        rc = fnConnectEx(confd, rp->ai_addr, rp->ai_addrlen, NULL, 0, NULL, &cpe->ovlap);
        if (rc || WSAGetLastError() == ERROR_IO_PENDING) {
            pdev->fd = confd;
            cpe->devid = pdev->id;
            pdev->family = rp->ai_family;
            pdev->socktype = rp->ai_socktype;
            pdev->protocol = rp->ai_protocol;

            freeaddrinfo(result);

            if (rc) {
                if (retval) *retval = 1;
                iocp_event_free(cpe);
                return NULL;
            }

            cpe->evtype = IOE_CONNECTED;
            cpe->fd = confd;
            cpe->state = IOCP_CONNECT_IN_PROGRESS;

            if (retval) *retval = 0;
            return cpe;

        } else {
            closesocket(confd);
            confd = -1;
            continue;
        }
    }

    freeaddrinfo(result);
    iocp_event_free(cpe);
 
    if (retval) *retval = -50;
    return NULL;
}

void * iocp_event_ep_connect_post (void * vdev, ep_sockaddr_t * host, char * lip, int lport, int * retval)
{
    iodev_t          * pdev = (iodev_t *)vdev;
    iocp_event_t     * cpe = NULL;
    int                ret = 0;
 
    SOCKET             confd = INVALID_SOCKET;
    ep_sockaddr_t      addr;
    BOOL               rc = FALSE;

    if (retval) *retval = -1;

    if (!pdev) return NULL;
    if (!host) return NULL;
 
    confd = WSASocket(host->family, host->socktype, IPPROTO_TCP,
                      NULL, 0, WSA_FLAG_OVERLAPPED);
    if (confd == INVALID_SOCKET) {
        if (retval) *retval = -10;
        return NULL;
    }

    ret = 1;
    setsockopt(confd, SOL_SOCKET, SO_REUSEADDR, (void *)&ret, sizeof(int));
#ifdef SO_REUSEPORT
    setsockopt(confd, SOL_SOCKET, SO_REUSEPORT, (void *)&ret, sizeof(int));
#endif
    setsockopt(confd, SOL_SOCKET, SO_KEEPALIVE, (void *)&ret, sizeof(int));
 
    sock_nonblock_set(confd, 1);

    memset(&addr, 0, sizeof(addr));
    if (lip && strlen(lip) > 0 && lport > 0) {
        ret = sock_addr_acquire(&addr, lip, lport, SOCK_STREAM);
        if (ret <= 0 || bind(confd, (struct sockaddr *)&addr.u.addr, addr.socklen) != 0) {
            if (ret > 1) sock_addr_freenext(&addr);
            closesocket(confd);
            confd = -1;
            if (retval) *retval = -20;
            return NULL;
        }

    } else {
        if (host->family == AF_INET) {
            addr.family = AF_INET;
            addr.socklen = sizeof(addr.u.addr4);
            addr.u.addr4.sin_family = AF_INET;
            addr.u.addr4.sin_addr.s_addr = INADDR_ANY;
            addr.u.addr4.sin_port = htons((uint16)lport);

        } else if (host->family == AF_INET6) {
            addr.family = AF_INET6;
            addr.socklen = sizeof(addr.u.addr6);
            addr.u.addr6.sin6_family = AF_INET;
            addr.u.addr6.sin6_addr = in6addr_any;
            addr.u.addr6.sin6_port = htons((uint16)lport);

        } else {
            closesocket(confd);
            confd = -1;
            if (retval) *retval = -30;
            return NULL;
        }

        if (bind(confd, (struct sockaddr *)&addr.u.addr, addr.socklen) != 0) {
            closesocket(confd);
            confd = -1;
            if (retval) *retval = -40;
            return NULL;
        }
    }

    cpe = iocp_event_alloc(0);
    if (!cpe) {
        closesocket(confd);
        if (retval) *retval = -50;
        return NULL;
    }
 
    rc = fnConnectEx(confd, &host->u.addr, host->socklen, NULL, 0, NULL, &cpe->ovlap);
    if (rc || WSAGetLastError() == ERROR_IO_PENDING) {
        pdev->fd = confd;
        cpe->devid = pdev->id;
        pdev->family = host->family;
        pdev->socktype = host->socktype;
        pdev->protocol = IPPROTO_TCP;

        if (rc) {
            if (retval) *retval = 1;
            iocp_event_free(cpe);
            return NULL;
        }

        cpe->evtype = IOE_CONNECTED;
        cpe->fd = confd;
        cpe->state = IOCP_CONNECT_IN_PROGRESS;

        if (retval) *retval = 0;
        return cpe;
    }

    closesocket(confd);
    confd = -1;
    iocp_event_free(cpe);
 
    if (retval) *retval = -60;
    return NULL;
}


int epcore_iocp_init (epcore_t * pcore)
{
    GUID acceptex = WSAID_ACCEPTEX; 
    GUID connectex = WSAID_CONNECTEX;
    GUID getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;  
    SOCKET  fd;
    DWORD   bytes = 0;
    int     num = 0;

    if (!pcore) return -1;

    if (pcore->iocp_port == NULL) {
        num = get_cpu_num();
        pcore->iocp_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                     NULL, 0, num << 1);
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
        return -2; 

    if (!fnAcceptEx) {
        WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &acceptex, sizeof(acceptex),
                 &fnAcceptEx, sizeof(fnAcceptEx),
                 &bytes, NULL, NULL);
    }

    if (!fnConnectEx) {
        WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &connectex, sizeof(connectex),
                 &fnConnectEx, sizeof(fnConnectEx),
                 &bytes, NULL, NULL);
    }

    if (!fnGetAcceptExSockaddrs) {
        WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &getacceptexsockaddrs, sizeof(getacceptexsockaddrs),
                 &fnGetAcceptExSockaddrs, sizeof(fnGetAcceptExSockaddrs),
                 &bytes, NULL, NULL);
    }

    closesocket(fd);

    return 0;
}

int epcore_iocp_clean (epcore_t * pcore)
{
    if (!pcore) return -1;

    if (pcore->iocp_port) {
        CloseHandle(pcore->iocp_port);
        pcore->iocp_port = NULL;
    }

    return 0;
}


int epump_iocp_init (epump_t * epump, int maxfd)
{
    return 0;
}

int epump_iocp_clean (epump_t * epump)
{
    return 0;
}

int epump_iocp_setpoll (void * vepump, void * vpdev)
{
    iodev_t   * pdev = (iodev_t *)vpdev;
    epcore_t  * pcore = NULL;
    HANDLE      hiocp = NULL;
    DWORD       err = 0;

    if (!pdev) return -1;

    pcore = pdev->epcore;
    if (!pcore) return -2;

    if (pdev->fd < 0) return -3;

    hiocp = CreateIoCompletionPort((HANDLE)pdev->fd, pcore->iocp_port, (ULONG_PTR)pdev, 0);
    if (hiocp == NULL) {
        err = GetLastError();
        tolog(1, "epump_iocp_setpoll: CreateIoCompletionPort failed for fd=%d pdev->id=%d error=%d\n", pdev->fd, pdev->id, err);
        return -100;
    }

    if (pdev->rwflag & RWF_READ) {
        //iocp_event_recv_post(pcore, pdev);
    } 
 
    if (pdev->rwflag & RWF_WRITE) {

    }

    return 0;
}


int epump_iocp_clearpoll (void * vepump, void * vpdev)
{
    iodev_t   * pdev = (iodev_t *)vpdev;

    if (!pdev) return -1;

    if (pdev->fd < 0) return -2;

    /* cancels all outstanding asynchronour I/O operations for the fd */
    CancelIoEx((HANDLE)pdev->fd, NULL);

    return 0;
}



int epump_iocp_dispatch (void * veps, btime_t * delay)
{
    epump_t      * epump = (epump_t *)veps;
    epcore_t     * pcore = NULL;
    int            waitms = 0;

    iocp_event_t * cpe = NULL;
    OVERLAPPED   * ovlap = NULL;
    ULONG_PTR      key = 0;
    DWORD          bytes = 0;
    BOOL           result;
    DWORD          err = 0;

    iodev_t         * pdev = NULL;
    iodev_t         * clidev = NULL;
    int               len;
    int               ret = 0;
    int               sockerr = 0;
    ep_sockaddr_t     sock;
    struct sockaddr * laddr = NULL;
    struct sockaddr * raddr = NULL;
    int               lalen = 0;
    int               ralen = 0;

    if (!epump) return -1;

    pcore = epump->epcore;
    if (!pcore) return -2;

    if (delay == NULL) {
        waitms = -1;
    } else {
        waitms = delay->s * 1000 + delay->ms;
        if (waitms > MAX_EPOLL_TIMEOUT_MSEC) waitms = MAX_EPOLL_TIMEOUT_MSEC;
    }

    result = GetQueuedCompletionStatus(pcore->iocp_port, &bytes, &key, &ovlap, waitms);
    if (!result) {
        err = WSAGetLastError();
        if (err == WAIT_TIMEOUT) {
            return 0;
        }
    }

    if (key == (ULONG_PTR)-1)  //PostQueuedCompletionStatus wakeup current thread
        return 0;

    if (!ovlap) return -1;

    /* iocp_event_t object should be managed by Hashtab or RBTree 
     * for verification before further operations */

    cpe = (iocp_event_t *)((uint8 *)ovlap - offsetof(iocp_event_t, ovlap));

    if (pcore->quit) {
        iocp_event_free(cpe);
        return 0;
    }

    pdev = (iodev_t *)key;
    if (!pdev) {
        iocp_event_free(cpe);
        return -101;
    }

    if (cpe->evtype == IOE_READ) {
        if (pdev->iocprecv > 0) pdev->iocprecv--;
    } else if (cpe->evtype == IOE_WRITE) {
        if (pdev->iocpsend > 0) pdev->iocpsend--;
    }

    if (pdev != epcore_iodev_find(pcore, cpe->devid)) {
        iocp_event_free(cpe);
        return -102;
    }

    if (!pdev->epump) {
        pdev->epump = epump;
        pdev->bindtype = BIND_ONE_EPUMP;
    }

    switch (cpe->evtype) {
    case IOE_ACCEPT:
        if (pdev->fdtype == FDT_LISTEN || pdev->fdtype == FDT_USOCK_LISTEN) {
            if (pdev->fdtype == FDT_LISTEN) {
                setsockopt(cpe->clifd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, 
                            (char *)&cpe->fd, sizeof(cpe->fd));

                clidev = iodev_new(pcore);
                if (!clidev) {
                    iocp_event_free(cpe);
                    return -200;
                }

                clidev->fd = cpe->clifd;
                cpe->clifd = INVALID_SOCKET;

                clidev->family = pdev->family;
                clidev->socktype = pdev->socktype;
                clidev->protocol = pdev->protocol;

                fnGetAcceptExSockaddrs(cpe->buf, 0, cpe->len/2, cpe->len/2, 
                                       &laddr, &lalen, &raddr, &ralen);
                sock_addr_ntop(laddr, clidev->local_ip);
                clidev->local_port = sock_addr_port(laddr);
                sock_addr_ntop(raddr, clidev->remote_ip);
                clidev->remote_port = sock_addr_port(raddr);

                if (pdev->devfifo == NULL) {
                    pdev->devfifo = ar_fifo_new(4);
                }
                ar_fifo_push(pdev->devfifo, clidev);

                /* post a new asynchronous operation waiting for accepting new connection */
                iocp_event_accept_post(pdev);
            }

            PushConnAcceptEvent(epump, pdev);
        }
        break;

    case IOE_CONNECTED:
        if (pdev->iostate == IOS_CONNECTING && pdev->fdtype == FDT_CONNECTED) {
            len = sizeof(int);
            sockerr = 0;
            ret = getsockopt(pdev->fd, SOL_SOCKET, SO_ERROR,
                                    (char *)&sockerr, (socklen_t *)&len);

            if (ret < 0 || sockerr != 0) {
                PushConnfailEvent(epump, pdev);

            } else {
                setsockopt(pdev->fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

                len = sizeof(sock);
                if (getsockname(pdev->fd, (struct sockaddr *)&sock,
                                (socklen_t *)&len) == 0) {
                    sock_addr_ntop(&sock, pdev->local_ip);
                    pdev->local_port = sock_addr_port(&sock);
                }

                len = sizeof(sock);
                if (getpeername(pdev->fd, (struct sockaddr *)&sock,
                               (socklen_t *)&len) == 0) {
                    sock_addr_ntop(&sock, pdev->remote_ip);
                    pdev->remote_port = sock_addr_port(&sock);
                }

                PushConnectedEvent(epump, pdev);
            }
        }
        break;

    case IOE_READ:
        if (pdev->fdtype == FDT_UDPSRV || pdev->fdtype == FDT_UDPCLI) {
            if (pdev->rcvfrm) frame_len_set(pdev->rcvfrm, bytes);
        }

        PushReadableEvent(epump, pdev);
        break;

    case IOE_WRITE:
        iodev_del_notify(pdev, RWF_WRITE);

        PushWritableEvent(epump, pdev);
        break;

    default:
        PushInvalidDevEvent(epump, pdev);
        break;
    }

    if (cpe) iocp_event_free(cpe);

    return 0;
}

#endif

