/*
 * Copyright (c) 2003-2021 Ke Hengzhong <kehengzhong@hotmail.com>
 * All rights reserved. See MIT LICENSE for redistribution.
 */

#ifdef HAVE_IOCP

#ifndef _EPIOCP_H_
#define _EPIOCP_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef BOOL WINAPI AcceptExPtr(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL WINAPI ConnectExPtr(SOCKET, const struct sockaddr *, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef void WINAPI GetAcceptExSockaddrsPtr(PVOID, DWORD, DWORD, DWORD, LPSOCKADDR *, LPINT, LPSOCKADDR *, LPINT);


#define IOCP_NONE                 0
#define IOCP_READ_IN_PROGRESS     1
#define IOCP_WRITE_IN_PROGRESS    2
#define IOCP_ACCEPT_IN_PROGRESS   3
#define IOCP_CONNECT_IN_PROGRESS  4

#define IOCP_BUF_CNT              16


typedef struct iocp_event_st {

    OVERLAPPED      ovlap;
    int             evtype;

    SOCKET          fd;
    SOCKET          clifd;

    ulong           devid;
    DWORD           flags;

    WSABUF          bufs[IOCP_BUF_CNT];
    uint16          bufcnt;
    int64           ionum;

    int64           mapsize;
    HANDLE          hmap;
    void          * pmap;

    uint16          state;

    int             len;
    uint8           buf[1];
} iocp_event_t;

void * iocp_event_alloc (int extsize);
void   iocp_event_free  (void * vcpe);

void * iocp_event_accept_post     (void * vdev);
void * iocp_event_recv_post       (void * vdev, void * pbuf, int len);
void * iocp_event_recvfrom_post   (void * vdev, void * pbuf, int len);
void * iocp_event_send_post       (void * vdev, void * chunk, int64 pos, int httpchunk);
void * iocp_event_connect_post    (void * vdev, char * host, int port, char * lip, int lport, int * retval);
void * iocp_event_ep_connect_post (void * vdev, ep_sockaddr_t * host, char * lip, int lport, int * retval);


int epcore_iocp_init (epcore_t * pcore);
int epcore_iocp_clean (epcore_t * pcore);

int epump_iocp_init (epump_t * epump, int maxfd);
int epump_iocp_clean (epump_t * epump);

int epump_iocp_setpoll (void * vepump, void * vpdev);
int epump_iocp_clearpoll (void * vepump, void * vpdev);

int epump_iocp_dispatch (void * veps, btime_t * delay);

#ifdef __cplusplus
}
#endif

#endif

#endif
